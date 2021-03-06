//
// Copyright (C) 2016 University of Amsterdam
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "spssimporter.h"
#include "./spss/spssrecinter.h"
#include "./spss/floatinforecord.h"
#include "./spss/variablerecord.h"
#include "./spss/valuelabelvarsrecord.h"
#include "./spss/vardisplayparamrecord.h"
#include "./spss/longvarnamesrecord.h"
#include "./spss/verylongstringrecord.h"
#include "./spss/extnumbercasesrecord.h"
#include "./spss/miscinforecord.h"
#include "./spss/documentrecord.h"
#include "./spss/characterencodingrecord.h"
#include "./spss/dictionaryterminationrecord.h"
#include "./spss/datarecords.h"

#include "sharedmemory.h"
#include "dataset.h"
#include "./spss/debug_cout.h"

using namespace std;

using namespace boost;
using namespace spss;


FileHeaderRecord *SPSSImporter::_pFileHeaderRecord = 0;
IntegerInfoRecord SPSSImporter::_integerInfo;
FloatInfoRecord SPSSImporter::_floatInfo;
double SPSSImporter::_fileSize = 0.0;



void SPSSImporter::killFhr()
{
	if (_pFileHeaderRecord != 0)
	{
		delete _pFileHeaderRecord;
		_pFileHeaderRecord = 0;
	}
}


void SPSSImporter::loadDataSet(
		DataSetPackage *packageData,
		const std::string &locator,
		boost::function<void (const std::string &, int)> progress)
{
	(void)progress;

	packageData->isArchive = false;						 // SPSS/spss files are never archives.
	packageData->dataSet = SharedMemory::createDataSet();   // Do our space.

	killFhr();

	// Open the file.
	SPSSStream stream(locator.c_str(), ios::in | ios::binary);

	// Get it's size
	stream.seekg(0, stream.end);
	_fileSize = static_cast<double>(stream.tellg());
	stream.seekg(0, stream.beg);

	// Data we have scraped to date.
	SPSSColumns dictData;

	// Fetch the dictionary.
	bool processingDict = true;

	while(stream.good() && processingDict)
	{
		// Inform user of progress.
		reportFileProgress(stream.tellg(), progress);

		// Get the record type.
		union { int32_t u; RecordTypes t; Char_4 c; } rec_type;
		rec_type.u = rectype_unknown;
		stream.read((char *) &rec_type.u, sizeof(rec_type.u));
		// Endiness for rec_type, if known.
		if (_pFileHeaderRecord != 0)
			dictData.numericsConv().fixup(&rec_type.u);

		// ... and the record type type is....
		switch(rec_type.t)
		{
		case FileHeaderRecord::RECORD_TYPE:
			_pFileHeaderRecord = new FileHeaderRecord(dictData.numericsConv(), rec_type.t, stream);
			_pFileHeaderRecord->process(dictData);
			break;

		case VariableRecord::RECORD_TYPE:
		{
			VariableRecord record(dictData.numericsConv(), rec_type.t, _pFileHeaderRecord, stream);
			record.process(dictData);
		}
			break;

		case ValueLabelVarsRecord::RECORD_TYPE:
		{
			ValueLabelVarsRecord record(dictData.numericsConv(), rec_type.t, stream);
			record.process(dictData);
		}
			break;

		case rectype_meta_data: // Need to find the type of the data..
		{
			union { int32_t i; RecordSubTypes s; } sub_type;
			sub_type.s = recsubtype_unknown;
			stream.read((char *) &sub_type.i, sizeof(sub_type.i));
			dictData.numericsConv().fixup(&sub_type.i);
			switch (sub_type.s)
			{
			case  IntegerInfoRecord::SUB_RECORD_TYPE:
			{
				_integerInfo = IntegerInfoRecord(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				_integerInfo.process(dictData);
			}
				break;

			case FloatInfoRecord::SUB_RECORD_TYPE:
			{
				_floatInfo = FloatInfoRecord(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				_floatInfo.process(dictData);
			}
				break;

			case VarDisplayParamRecord::SUB_RECORD_TYPE:
			{
				VarDisplayParamRecord record(dictData.numericsConv(), sub_type.s, rec_type.t, dictData.size(), stream);
				record.process(dictData);
			}
				break;

			case LongVarNamesRecord::SUB_RECORD_TYPE:
			{
				LongVarNamesRecord record(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				record.process(dictData);
			}
				break;

			case VeryLongStringRecord::SUB_RECORD_TYPE:
			{
				VeryLongStringRecord record(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				record.process(dictData);
			}
				break;

			case ExtNumberCasesRecord::SUB_RECORD_TYPE:
			{
				ExtNumberCasesRecord record(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				record.process(dictData);
			}
				break;

			case CharacterEncodingRecord::SUB_RECORD_TYPE:
			{
				CharacterEncodingRecord record(dictData.numericsConv(), sub_type.s, rec_type.t, stream);
				record.process(dictData);
			}
				break;

			default:
			{
				MiscInfoRecord record(dictData.numericsConv(), sub_type.i, rec_type.t, stream);
				record.process(dictData);
			}
			}
		}
			break;

		case DocumentRecord::RECORD_TYPE:
		{
			DocumentRecord dummy(dictData.numericsConv(), rec_type.t, stream);
			dummy.process(dictData);
		}
			break;

		case DictionaryTermination::RECORD_TYPE:
		{
			DictionaryTermination dummy(dictData.numericsConv(), rec_type.t, stream);
			dummy.process(dictData);
		}
			processingDict = false; // Got end of dictionary.
			break;

		case rectype_unknown:
		default:
        {
			string msg("Unknown record type '"); msg.append(rec_type.c, sizeof(rec_type.c)); msg.append("' found.\n"
				"The SAV importer cannot yet read this file.\n"
				"Please report this error at \n"
				"https://github.com/jasp-stats/jasp-desktop/issues\n"
				"including a small sample .SAV file that produces this message.");
			throw runtime_error(msg);
			break;
		}
        }
	}

	//If we got a file header then..
	if (_pFileHeaderRecord == 0)
		throw runtime_error("No header found in .SAV file.");

	// Now convert the string in the header that we are interested in.,
	ConvertedStringContainer::processAllStrings(dictData.stringsConv());

	// read the data records from the file.
	DataRecords data(dictData.numericsConv(), *_pFileHeaderRecord, dictData, stream, progress);
	data.read(packageData);

	dictData.processStringsPostLoad(progress);


	DEBUG_COUT5("Read ", data.numDbls(), " doubles and ", data.numStrs(), " string cells.");


	// bail if unknown number of cases.
	if (dictData.hasNoCases())
		throw runtime_error("Found no cases in .SAV file.");

	// Set the data size
	setDataSetSize(*packageData, dictData.numCases(), dictData.size());

	bool success;
	do {

		success = true;
		try {
			// Now go fetch the data.
			for (size_t i = 0; i < dictData.size(); i++)
			{
				SPSSColumn &spssCol = dictData.getColumn(i);
				Column &column = packageData->dataSet->column(i);

				column.setName(spssCol.spssColumnLabel());
				column.setColumnType( spssCol.getJaspColumnType() );
				column.labels().clear();

				DEBUG_COUT3("Getting col: ", i, ",\n");

				switch(column.columnType())
				{
				default:	// Skip unknown columns
					break;
				case Column::ColumnTypeScale:
					setColumnScaleData(column, dictData.numCases(), spssCol);
					break;

				case Column::ColumnTypeNominal:
				case Column::ColumnTypeOrdinal:
					setColumnLabeledData(column, dictData.numCases(), spssCol);
					break;

				case Column::ColumnTypeNominalText:
					switch(spssCol.cellType())
					{
					case SPSSColumn::cellString:
						setColumnConvrtStringData(column, dictData.stringsConv(), spssCol);
						break;

					case SPSSColumn::cellDouble:
						 // convert to UTF-8 strings.
						spssCol.strings.clear();
						for (size_t i = 0; i < spssCol.numerics.size(); i++)
							spssCol.strings.push_back( spssCol.format(spssCol.numerics[i]) );
						column.setColumnAsNominalString(spssCol.strings);
						break;
					}
					break;
				}
			}
		}
		catch (boost::interprocess::bad_alloc &e)
		{
			try {

				packageData->dataSet = SharedMemory::enlargeDataSet(packageData->dataSet);
				success = false;
			}
			catch (boost::exception &e)
			{
				throw runtime_error("Out of memory: this data set is too large for your computer's available memory");
			}
		}
		catch (std::exception e)
		{
			cout << "n " << e.what();
			cout.flush();
		}
		catch (...)
		{
			cout << "something else\n ";
			cout.flush();
		}

	} while (success == false);


	if (stream.bad())
	{
		killFhr();
		SharedMemory::deleteDataSet(packageData->dataSet);
		throw runtime_error("Error reading .SAV file.");
	}
};


/**
 * @brief setDataSetSize Sets the data set size in the passed
 * @param dataSet The Data set to manipluate.
 * @param rowCount The (real) number of rows we have.
 * @param colCount The number of columns.
 */
void SPSSImporter::setDataSetSize(DataSetPackage &dataSetPg, size_t rowCount, size_t colCount)
{
	bool retry(true);
	do
	{
		try {

			dataSetPg.dataSet->setColumnCount(colCount);
			dataSetPg.dataSet->setRowCount(rowCount);
			retry = false;
		}
		catch (boost::interprocess::bad_alloc &e)
		{

			try {

				dataSetPg.dataSet = SharedMemory::enlargeDataSet(dataSetPg.dataSet);
				retry = false;
			}
			catch (std::exception e)
			{
				DEBUG_COUT2("SPSSImporter::setDataSetSize(), reallocating. std::exeption: ", e.what());
				throw runtime_error("Out of memory: this data set is too large for your computer's available memory");
			}
		}
		catch (std::exception e)
		{
			DEBUG_COUT2("SPSSImporter::setDataSetSize() std::exeption: ", e.what());
			throw runtime_error("Exception when reading .SAV file.");
		}
		catch (...)
		{
			DEBUG_COUT1("SPSSImporter::setDataSetSize() unknown exception.");
			throw runtime_error("Unidentified error when reading .SAV file.");
		}
	}
	while (retry);
}


/**
 * @brief ReportProgress Reports progress for stream.
 * @param position Position to report.
 * @param progress report to here.
 */
void SPSSImporter::reportFileProgress(SPSSStream::pos_type position, boost::function<void (const std::string &, int)> prgrss)
{
	static int lastPC = -1.0;
	int thisPC = static_cast<int>((100.0 * static_cast<double>(position) / _fileSize) + 0.5);
	if (lastPC != thisPC)
	{
		int pg = static_cast<int>(thisPC + 0.5);
		prgrss("Loading .SAV file", pg);
		lastPC = thisPC;
	}
}

/**
 * @brief setColumnScaleData Sets floating point data into the column.
 * @param column The columns to insert into.
 * @param spssCol The Source of the data.
 */
void SPSSImporter::setColumnScaleData(Column &column, size_t numCases, const SPSSColumn &spssCol)
{
	vector<double> values = spssCol.numerics;
	while (values.size() < numCases)
		values.push_back(NAN);
	if (values.size() > numCases)
		values.resize(numCases);
	// Have the columns do most of the work.
	column.setColumnAsScale(values);
}

/**
 * @brief setColumnConvrtStringData Sets String data into the column.
 * @param column The columns to insert into.
 * @param numCases The number of cases.
 * @param spssCol The Source of the data.
 */
void SPSSImporter::setColumnConvrtStringData(Column &column, CodePageConvert &strConvertor, spss::SPSSColumn &spssCol)
{
	// Code page convert all strings.
	for (size_t i = 0; i < spssCol.strings.size(); ++i)
		spssCol.strings[i] = strConvertor.convertCodePage(spssCol.strings[i]);

	column.setColumnAsNominalString(spssCol.strings);
}


void SPSSImporter::setColumnLabeledData(Column &column, size_t numCases, const SPSSColumn &spssCol)
{
	column.labels().clear();

	// Generate a lable for each unique floating point value.
	map<double, string> lbs;
	for (size_t i = 0; i < numCases; ++i)
		lbs.insert( pair<double, string>( spssCol.numerics[i], spssCol.format(spssCol.numerics[i])) );

	// Add / overwrite any values in the lables header.
	for (SPSSColumn::LabelByValueDict::const_iterator it = spssCol.spssLables.begin();
			it != spssCol.spssLables.end(); ++it)
	{
		// Got an label for this value?
		{
			map<double, string>::iterator iv = lbs.find(it->first.dbl);
			if (iv != lbs.end())
				lbs.erase(iv);
		}
		lbs.insert( pair<double, string>(it->first.dbl, it->second) );
	}

	// Extract the data were are going to use.
	vector<int> dataToInsert;
	map<int, string> labels;
	// We cannot insert doubles as data valuesm and get labels
	// for them to work (JASP limitation).
	if (spssCol.containsFraction())
	{
		// Generate an index value for each data point.
		for (size_t i = 0; i < numCases; ++i)
		{
			// Find insert the index as a data point.
			dataToInsert.push_back( distance(lbs.begin(), lbs.find(spssCol.numerics[i]) ) );
			// pair the insrted value with a lable string.
			labels.insert(pair<int, string>(dataToInsert.back(), lbs.find(spssCol.numerics[i])->second));
		}
	}
	else
	{
		// USe the raw data as the index to labels.
		for (size_t i = 0; i < numCases; ++i)
		{
			// insert the (rounded) value as the data point.
			dataToInsert.push_back( static_cast<int>(spssCol.numerics[i]));
			// pair the insrted value with a lable string.
			labels.insert(pair<int, string>(dataToInsert.back(), lbs.find(spssCol.numerics[i])->second));
		}
	}

	// Insert the labels into the JASP data set.
	for (map<int, string>::const_iterator it = labels.begin(); it != labels.end(); ++it)
		column.labels().add(it->first, it->second);

	// Insert the data into the data set.
	Column::Ints::iterator intInputItr = column.AsInts.begin();
	for (size_t i = 0; i < dataToInsert.size(); ++i, ++intInputItr)
		*intInputItr = dataToInsert[i];
}
