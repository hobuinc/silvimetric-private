// LASFormatFile.cpp: implementation of the CLASFormatFile class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "LASFormatFile.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLASFormatFile::CLASFormatFile()
{
	m_FileHandle = NULL;
	m_ValidHeader = FALSE;
	m_CurrentPointIndex = -1;

	ExtraByteCount = 0;

	// initialize the point record lengths
	for (int i = 0; i < 32; i ++)
		PointFormatRecordLength[i] = 0;

	// set record lengths
	PointFormatRecordLength[0] = 20;
	PointFormatRecordLength[1] = 28;
	PointFormatRecordLength[2] = 26;
	PointFormatRecordLength[3] = 34;
	PointFormatRecordLength[4] = 57;
	PointFormatRecordLength[5] = 63;
	PointFormatRecordLength[6] = 30;
	PointFormatRecordLength[7] = 36;
	PointFormatRecordLength[8] = 38;
	PointFormatRecordLength[9] = 59;
	PointFormatRecordLength[10] = 67;		// was 65 12/22/2016 but this didn't include the 2 bytes for NIR field. r13 of the LAS 1.4 spec has an error in that it says that format 10 is the same as format 7 with the addition of wave packets (should be format 8)
}

CLASFormatFile::~CLASFormatFile()
{
	Close();
}

// header block
CLASPublicHeaderBlock::CLASPublicHeaderBlock()
{
	// initialize header
	strcpy(FileSignature, "LASF");
	Reserved.Reserved = 0;
	GUIDData1 = 0;
	GUIDData2 = 0;
	GUIDData3 = 0;
	memset(GUIDData4, 0, 9);
	VersionMajor = 1;
	VersionMinor = 0;
	memset(SystemIdentifier, 0, 33);
	memset(GeneratingSoftware, 0, 33);
	FlightDateJulian = 0;
	Year = 0;
	HeaderSize = HEADERSIZE;
	OffsetToData = HEADERSIZE;		// leave some space for variable length records 1024 plus start of data signature
	NumberOfVariableLengthRecords = 0;
	PointDataFormatID = 0;
	PointDataRecordLength = 20;
	LegacyNumberOfPointRecords = 0;
	LegacyNumberOfPointsByReturn[0] = 0;
	LegacyNumberOfPointsByReturn[1] = 0;
	LegacyNumberOfPointsByReturn[2] = 0;
	LegacyNumberOfPointsByReturn[3] = 0;
	LegacyNumberOfPointsByReturn[4] = 0;
	XScaleFactor = YScaleFactor = ZScaleFactor = 1.0;
	XOffset = YOffset = ZOffset = 0.0;
	MinX = MinY = MinZ = MaxX = MaxY = MaxZ = 0.0;
	WaveformStart = 0;
	StartOfExtendedVLR = 0;
	NumberOfExtendedVLRs = 0;
	ExtendedNumberOfPointRecords = 0;
	NumberOfPointRecords = 0;
	for (int i = 0; i < 15; i ++) {
		ExtendedNumberOfPointsByReturn[i] = 0;
		NumberOfPointsByReturn[i] = 0;
	}

	UserDataInHeaderSize = 0;
	UserDataInHeader = NULL;

	UserDataAfterHeaderSize = 0;
	UserDataAfterHeader = NULL;
}

CLASPublicHeaderBlock::~CLASPublicHeaderBlock()
{
	if (UserDataInHeaderSize) {
		delete [] UserDataInHeader;
		UserDataInHeaderSize = 0;
		UserDataInHeader = NULL;
	}

	if (UserDataAfterHeaderSize) {
		delete[] UserDataAfterHeader;
		UserDataAfterHeaderSize = 0;
		UserDataAfterHeader = NULL;
	}
}

// variable length record
CLASVariableLengthRecord::CLASVariableLengthRecord()
{
	RecordSignature = 0;				// record signature for variable length record...0xAABB
	UserID[0] = '\0';					// ASCII user identifier for creator of variable length record
	RecordID = 0;						// record identifier
	RecordLengthAfterHeader = 0;		// number of bytes in record after the standard portion of th header
	Description[0] = '\0';				// record description
}

CLASVariableLengthRecord::~CLASVariableLengthRecord()
{
}

// variable length record
CLASExtendedVariableLengthRecord::CLASExtendedVariableLengthRecord()
{
	RecordSignature = 0;				// record signature for variable length record...0xAABB
	UserID[0] = '\0';					// ASCII user identifier for creator of variable length record
	RecordID = 0;						// record identifier
	RecordLengthAfterHeader = 0;		// number of bytes in record after the standard portion of th header
	Description[0] = '\0';				// record description
}

CLASExtendedVariableLengthRecord::~CLASExtendedVariableLengthRecord()
{
}

// point class...format 0
CLASPointDataRecord::CLASPointDataRecord()
{
	X = Y = Z = 0;
	Intensity = 0;
	ReturnNumber = 0;
	NumberOfReturns = 0;
	ScanDirectionFlag = 0;
	EdgeOfFlightLineFlag = 0;
	Classification = 0;
	ScanAngleRank = 0;
	FileMarker = 0;
	UserBitField = 0;
	ExtraByteCount = 0;
}

CLASPointDataRecord::~CLASPointDataRecord()
{

}

void CLASPointDataRecord::UnpackReturnBitInfo(BOOL FixFormatError)
{
	if (FixFormatError) {
		ReturnNumber = (ReturnBitInfo & 0xE0) >> 5;
		NumberOfReturns = (ReturnBitInfo & 0x1C) >> 2;
		ScanDirectionFlag = 0;
		EdgeOfFlightLineFlag = 0;
	}
	else {
		ReturnNumber = ReturnBitInfo & 0x07;
		NumberOfReturns = (ReturnBitInfo & 0x38) >> 3;
		ScanDirectionFlag = (ReturnBitInfo & 0x40) >> 6;
		EdgeOfFlightLineFlag = (ReturnBitInfo & 0x80) >> 7;
	}
}

void CLASPointDataRecord::PackReturnBitInfo()
{
	ReturnBitInfo = 0;
	ReturnBitInfo |= (ReturnNumber & 0x07);
	ReturnBitInfo |= (NumberOfReturns & 0x07) << 3;
	ReturnBitInfo |= (ScanDirectionFlag & 0x01) << 6;
	ReturnBitInfo |= (EdgeOfFlightLineFlag & 0x01) << 7;
}

// point class...format 1
//CLASPointDataRecordFormat1::CLASPointDataRecordFormat1() : CLASPointDataRecord()
//{
//	GPSTime = 0.0;
//}
//
//CLASPointDataRecordFormat1::~CLASPointDataRecordFormat1()
//{
//
//}

// point class...generic format 0-3
CLASPointDataRecordFormatAll::CLASPointDataRecordFormatAll() : CLASPointDataRecord()
{
	GPSTime = 0.0;
	Red = Green = Blue = NIR = 0;
	WavePacketDescriptorIndex = 0;
	ByteOffsetToWaveformData = 0;
	WaveformPacketSizeInBytes = 0;
	ReturnPointWaveformLocation = 0.0;
	Xt = 0.0;
	Yt = 0.0;
	Zt = 0.0;
	ClassificationBitInfo = 0;
	ClassificationFlags = 0;
	ScannerChannel = 0;
	ScanAngle = 0;
}

CLASPointDataRecordFormatAll::~CLASPointDataRecordFormatAll()
{

}
// classification info
CLASClassificationLookupTable::CLASClassificationLookupTable()
{
	// allocate space for classification table
	Class = new CLASSIFICATION[256];
}

CLASClassificationLookupTable::~CLASClassificationLookupTable()
{
	// free space for classification table
	delete[] Class;
}

// flight line info
CLASFlightLineHeaderLookupTable::CLASFlightLineHeaderLookupTable()
{
	// allocate memory for flightline table
	FlightLine = new FLIGHTLINE[256];
}

CLASFlightLineHeaderLookupTable::~CLASFlightLineHeaderLookupTable()
{
	// free memory for flightline table
	delete[] FlightLine;
}

// histogram
CLASHistogram::CLASHistogram()
{
}

CLASHistogram::~CLASHistogram()
{
}

// text description
CLASTextAreaDescription::CLASTextAreaDescription()
{
}

CLASTextAreaDescription::~CLASTextAreaDescription()
{
}

BOOL CLASFormatFile::VerifyFormat(LPCTSTR FileName)
{
	BOOL retcode = FALSE;
	char sig[5];

	// open file and read first 4 bytes...look for signature "LASF"
	FILE* f = fopen(FileName, "rb");
	if (f) {
		if (fread(sig, sizeof(char), 4, f) == 4) {
			sig[4] = '\0';
			if (strcmp(sig, "LASF") == 0)
				retcode = TRUE;
		}
		fclose(f);
	}
	return(retcode);
}

BOOL CLASFormatFile::Open(LPCTSTR FileName, char *buffer, int buffersize)
{
	BOOL retcode = FALSE;

	if (VerifyFormat(FileName)) {
		// open file
		m_FileHandle = fopen(FileName, "rb");
		if (m_FileHandle) {
			// set up a larger buffer...buffer is managed by calling code
			if (buffer) {
				if (setvbuf(m_FileHandle, buffer, _IOFBF, buffersize) == 0) {
					retcode = Header.Read(m_FileHandle);
				}
				else {
					// can't attach buffer...close file
					fclose(m_FileHandle);
					m_FileHandle = NULL;
					retcode = FALSE;
				}
			}
			else
				retcode = Header.Read(m_FileHandle);
		}
	}
	m_ValidHeader = retcode;

	// deal with extra bytes
	if (retcode)
		ExtraByteCount = Header.PointDataRecordLength - PointFormatRecordLength[Header.PointDataFormatID];
	else
		ExtraByteCount = 0;

	return(retcode);
}

BOOL CLASPublicHeaderBlock::Read(FILE *FileHandle)
{
	int i;
	BOOL retcode = FALSE;

	if (FileHandle) {
		if (UserDataInHeaderSize) {
			delete [] UserDataInHeader;
			UserDataInHeaderSize = 0;
			UserDataInHeader = NULL;
		}

		if (UserDataAfterHeaderSize) {
			delete[] UserDataAfterHeader;
			UserDataAfterHeaderSize = 0;
			UserDataAfterHeader = NULL;
		}

		// read all fields from header...may not always work to read as structure since byte packing differs depending on compiler options
		int cnt = 0;
		cnt += (int) fread(FileSignature, sizeof(char), 4, FileHandle);
		cnt += (int) fread(&Reserved, sizeof(ulong), 1, FileHandle);
		cnt += (int) fread(&GUIDData1, sizeof(ulong), 1, FileHandle);
		cnt += (int) fread(&GUIDData2, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(&GUIDData3, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(GUIDData4, sizeof(char), 8, FileHandle);
		cnt += (int) fread(&VersionMajor, sizeof(uchar), 1, FileHandle);
		cnt += (int) fread(&VersionMinor, sizeof(uchar), 1, FileHandle);
		cnt += (int) fread(SystemIdentifier, sizeof(char), 32, FileHandle);
		cnt += (int) fread(GeneratingSoftware, sizeof(char), 32, FileHandle);
		cnt += (int) fread(&FlightDateJulian, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(&Year, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(&HeaderSize, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(&OffsetToData, sizeof(ulong), 1, FileHandle);
		cnt += (int) fread(&NumberOfVariableLengthRecords, sizeof(ulong), 1, FileHandle);
		cnt += (int) fread(&PointDataFormatID, sizeof(uchar), 1, FileHandle);
		cnt += (int) fread(&PointDataRecordLength, sizeof(ushort), 1, FileHandle);
		cnt += (int) fread(&LegacyNumberOfPointRecords, sizeof(ulong), 1, FileHandle);
		cnt += (int) fread(LegacyNumberOfPointsByReturn, sizeof(ulong), 5, FileHandle);
		cnt += (int) fread(&XScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&YScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&ZScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&XOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&YOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&ZOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MaxX, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MinX, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MaxY, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MinY, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MaxZ, sizeof(double), 1, FileHandle);
		cnt += (int) fread(&MinZ, sizeof(double), 1, FileHandle);

		// terminate strings
		FileSignature[4] = '\0';
		GUIDData4[8] = '\0';
		SystemIdentifier[32] = '\0';
		GeneratingSoftware[32] = '\0';

		// build the version identifier
		char ts[32];
		sprintf(ts, "%i.%i", VersionMajor, VersionMinor);
		Version = (float) atof(ts);

		// for all versions, copy the number of points and number of returns to the "old" variables
		// if we are reading a V1.4+ file, these values will be overwritten by the values in the extended counters
		NumberOfPointRecords = (unsigned __int64) LegacyNumberOfPointRecords;
		for (i = 0; i < 5; i ++) {
			NumberOfPointsByReturn[i] = (unsigned __int64) LegacyNumberOfPointsByReturn[i];
		}

		// if using reading version 1.3, read start of waveform packet record
		if (VersionMajor == 1 && VersionMinor > 2) {
			cnt += (int) fread(&WaveformStart, sizeof(unsigned __int64), 1, FileHandle);
		}

		if (VersionMajor == 1 && VersionMinor > 3) {
			cnt += (int) fread(&StartOfExtendedVLR, sizeof(unsigned __int64), 1, FileHandle);
			cnt += (int) fread(&NumberOfExtendedVLRs, sizeof(ulong), 1, FileHandle);
			cnt += (int) fread(&ExtendedNumberOfPointRecords, sizeof(unsigned __int64), 1, FileHandle);
			cnt += (int) fread(ExtendedNumberOfPointsByReturn, sizeof(unsigned __int64), 15, FileHandle);

			// copy number of returns and return counts to legacy fields if the number of points does not exceed UINT_MAX
//			if (NumberOfPointRecords == 0 && ExtendedNumberOfPointRecords < 0xffffffff) {

			// 1/24/2017 changed variable types to accomodate V1.4 LAS files
			if (NumberOfPointRecords == 0 && ExtendedNumberOfPointRecords > 0) {
				NumberOfPointRecords = ExtendedNumberOfPointRecords;
				for (i = 0; i < 15; i ++) {
					NumberOfPointsByReturn[i] = ExtendedNumberOfPointsByReturn[i];
				}
			}
			// removed this code 1/24/2017 when I changed the variable types for total return and return number counts
//			else if (ExtendedNumberOfPointRecords >= 0xffffffff) {
//				// too many points
//				return(FALSE);
//			}
			// assume legacy mode where number of returns in legacy header variables and return counts are valid
		}

		// check the header size to see if there is any extra data in the header
		bool ExtraDataInHeader = false;
		if (VersionMajor == 1) {
			if (VersionMinor <= 2 && HeaderSize > HEADERSIZEV12) {
				ExtraDataInHeader = true;
				UserDataInHeaderSize = HeaderSize - HEADERSIZEV12;
			}
			else if (VersionMinor == 3 && HeaderSize > HEADERSIZEV13) {
				ExtraDataInHeader = true;
				UserDataInHeaderSize = HeaderSize - HEADERSIZEV13;
			}
			else if (VersionMinor == 4 && HeaderSize > HEADERSIZEV14) {
				ExtraDataInHeader = true;
				UserDataInHeaderSize = HeaderSize - HEADERSIZEV14;
			}
		}

		if (ExtraDataInHeader) {
			// allocate space for extra header data and read it in
			UserDataInHeader = new unsigned char[UserDataInHeaderSize];
			if (UserDataInHeader) {
				cnt += (int) fread(UserDataInHeader, sizeof(unsigned char), UserDataInHeaderSize, FileHandle);
			}
		}

		// check the header size to see if there is any extra data after the header
		// this is harder since we need the size of the VLRs and the extra data comes after the VLRs
		bool ExtraDataAfterHeader = false;
		unsigned long TotalVLRLength = 0;
		CLASVariableLengthRecord VarRec;
		if (NumberOfVariableLengthRecords) {
			fseek(FileHandle, HeaderSize, SEEK_SET);

			for (unsigned long i = 0; i < NumberOfVariableLengthRecords; i ++) {
				// read the VLR header
				VarRec.Read(FileHandle);

				// jump to end of record data
				fseek(FileHandle, VarRec.RecordLengthAfterHeader, SEEK_CUR);

				TotalVLRLength += (VLRHEADERSIZE + VarRec.RecordLengthAfterHeader);
			}
		}

		if (HeaderSize + TotalVLRLength < OffsetToData) {
			ExtraDataAfterHeader = true;

			UserDataAfterHeaderSize = OffsetToData - (HeaderSize + TotalVLRLength);
		}

		if (ExtraDataAfterHeader) {
			// allocate space for extra header data and read it in
			UserDataAfterHeader = new unsigned char[UserDataAfterHeaderSize];
			if (UserDataAfterHeader) {
				cnt += (int) fread(UserDataAfterHeader, sizeof(unsigned char), UserDataAfterHeaderSize, FileHandle);
			}
		}

		// cnt should be 107...total number of values read including individual characters in strings
		// cnt should be 108 for file versions 1.3...need to read the start of waveform data
		// cnt should be 126 for file version 1.4+
		if (VersionMajor == 1 && VersionMinor > 3) {
			if ((unsigned long) cnt == (126 + UserDataInHeaderSize + UserDataAfterHeaderSize))
				retcode = TRUE;
		}
		else if (VersionMajor == 1 && VersionMinor > 2) {
			if ((unsigned long) cnt == (108 + UserDataInHeaderSize + UserDataAfterHeaderSize))
				retcode = TRUE;
		}
		else {
			if ((unsigned long) cnt == (107 + UserDataInHeaderSize + UserDataAfterHeaderSize))
				retcode = TRUE;
		}
	}
	return(retcode);
}

BOOL CLASPublicHeaderBlock::Write(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	if (FileHandle) {
		int i = 0;
		int start = 0;

		// clean up the strings...need to pad with null chars
		for (i = (int) strlen(FileSignature); i <= 4; i ++)
			FileSignature[i] = '\0';

		for (i = 0; i <= 8; i ++) {
			if (GUIDData4[i] == 0) {
				start = i;
				break;
			}
		}
		for (i = start; i <= 8; i ++)
			GUIDData4[i] = 0;

		for (i = (int) strlen(SystemIdentifier); i <= 32; i ++)
			SystemIdentifier[i] = '\0';
		for (i = (int) strlen(GeneratingSoftware); i <= 32; i ++)
			GeneratingSoftware[i] = '\0';

		// deal with point counters for V1.4+ files
		if (VersionMajor == 1 && VersionMinor > 3) {
			ExtendedNumberOfPointRecords = NumberOfPointRecords;
			for (i = 0; i < 15; i++) {
				ExtendedNumberOfPointsByReturn[i] = NumberOfPointsByReturn[i];
			}

			// only change counts to 0 if using record types 6+ or there are more than 2 billion points
			if (PointDataFormatID >= 6 || NumberOfPointRecords > ULONG_MAX) {
				LegacyNumberOfPointRecords = 0;
				for (i = 0; i < 5; i++) {
					LegacyNumberOfPointsByReturn[i] = 0;
				}
			}
			else {
				LegacyNumberOfPointRecords = (ulong) NumberOfPointRecords;
				for (i = 0; i < 5; i++) {
					LegacyNumberOfPointsByReturn[i] = (ulong) NumberOfPointsByReturn[i];
				}
			}
		}
		else {
			LegacyNumberOfPointRecords = (ulong) NumberOfPointRecords;
			for (i = 0; i < 5; i ++) {
				LegacyNumberOfPointsByReturn[i] = (ulong) NumberOfPointsByReturn[i];
			}
		}

		// write all fields from header...may not always work to read as structure since byte packing differs depending on compiler options
		int cnt = 0;
		cnt += (int) fwrite(FileSignature, sizeof(char), 4, FileHandle);
		cnt += (int) fwrite(&Reserved, sizeof(ulong), 1, FileHandle);
		cnt += (int) fwrite(&GUIDData1, sizeof(ulong), 1, FileHandle);
		cnt += (int) fwrite(&GUIDData2, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(&GUIDData3, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(GUIDData4, sizeof(char), 8, FileHandle);
		cnt += (int) fwrite(&VersionMajor, sizeof(uchar), 1, FileHandle);
		cnt += (int) fwrite(&VersionMinor, sizeof(uchar), 1, FileHandle);
		cnt += (int) fwrite(SystemIdentifier, sizeof(char), 32, FileHandle);
		cnt += (int) fwrite(GeneratingSoftware, sizeof(char), 32, FileHandle);
		cnt += (int) fwrite(&FlightDateJulian, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(&Year, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(&HeaderSize, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(&OffsetToData, sizeof(ulong), 1, FileHandle);
		cnt += (int) fwrite(&NumberOfVariableLengthRecords, sizeof(ulong), 1, FileHandle);
		cnt += (int) fwrite(&PointDataFormatID, sizeof(uchar), 1, FileHandle);
		cnt += (int) fwrite(&PointDataRecordLength, sizeof(ushort), 1, FileHandle);
		cnt += (int) fwrite(&LegacyNumberOfPointRecords, sizeof(ulong), 1, FileHandle);
		cnt += (int) fwrite(LegacyNumberOfPointsByReturn, sizeof(ulong), 5, FileHandle);
		cnt += (int) fwrite(&XScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&YScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&ZScaleFactor, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&XOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&YOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&ZOffset, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MaxX, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MinX, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MaxY, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MinY, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MaxZ, sizeof(double), 1, FileHandle);
		cnt += (int) fwrite(&MinZ, sizeof(double), 1, FileHandle);

		// if using reading version 1.3, read start of waveform packet record
		if (VersionMajor == 1 && VersionMinor > 2) {
			cnt += (int) fwrite(&WaveformStart, sizeof(unsigned __int64), 1, FileHandle);
		}

		if (VersionMajor == 1 && VersionMinor > 3) {
			// modified code for V1.4...12/21/2016
			StartOfExtendedVLR = 0;
			NumberOfExtendedVLRs = 0;
			// end of modified code for V1.4...12/21/2016
			cnt += (int) fwrite(&StartOfExtendedVLR, sizeof(unsigned __int64), 1, FileHandle);
			cnt += (int) fwrite(&NumberOfExtendedVLRs, sizeof(ulong), 1, FileHandle);
			cnt += (int) fwrite(&ExtendedNumberOfPointRecords, sizeof(unsigned __int64), 1, FileHandle);
			cnt += (int) fwrite(ExtendedNumberOfPointsByReturn, sizeof(unsigned __int64), 15, FileHandle);
		}

		// cnt should be 107...total number of values written including individual characters in strings
		// cnt should be 108 for file versions 1.3 and higher...need to read the start of waveform data
		// cnt should be 126 for file version 1.4+
		if (VersionMajor == 1 && VersionMinor > 3) {
			if (cnt == 126)
				retcode = TRUE;
		}
		else if (VersionMajor == 1 && VersionMinor > 2) {
			if (cnt == 108)
				retcode = TRUE;
		}
		else {
			if (cnt == 107)
				retcode = TRUE;
		}
	}
	return(retcode);
}

void CLASFormatFile::JumpToPointRecord(int index)
{
	if (IsValid()) {
		fseek(m_FileHandle, Header.OffsetToData + (index * Header.PointDataRecordLength), SEEK_SET);
		m_CurrentPointIndex = index;
	}
}

BOOL CLASFormatFile::IsValid()
{
	return(m_ValidHeader && m_FileHandle != NULL);
}

BOOL CLASFormatFile::ReadNextPoint()
{
	if (!IsValid())
		return(FALSE);

	// see if we have read all the points...needed with EVLRs which are located at the end of the data...added 1/4/2017
	if ((unsigned long) m_CurrentPointIndex >= Header.NumberOfPointRecords)
		return(FALSE);

	// if we haven't done offset to start of data, do it now
	if (m_CurrentPointIndex < 0)
		JumpToPointRecord();

	// read a point
	BOOL retcode = TRUE;
	if (Header.PointDataFormatID > 5) {		// new base values for point formats 6+ (LAS V1.4)
		retcode = PointRecord.ReadBaseValues14plus(m_FileHandle);
	}
	else
		retcode = PointRecord.ReadBaseValues(m_FileHandle);

	if (retcode && Header.PointDataFormatID > 5)
		retcode = PointRecord.ReadAdditionalValues14plus(m_FileHandle, Header.PointDataFormatID);
	else if (retcode && Header.PointDataFormatID > 0)
		retcode = PointRecord.ReadAdditionalValues(m_FileHandle, Header.PointDataFormatID);

	// see if there are extra bytes in the point records...difference between point record length in header and length of point format
	if (ExtraByteCount)
		PointRecord.ReadExtraBytes(m_FileHandle, ExtraByteCount);

	if (retcode) {
		// do scale and offset correction
		PointRecord.ScaleX(Header.XOffset, Header.XScaleFactor);
		PointRecord.ScaleY(Header.YOffset, Header.YScaleFactor);
		PointRecord.ScaleZ(Header.ZOffset, Header.ZScaleFactor);

		// if reading version 1.1 or newer, extract bits from classification field
		if (Header.Version > 1.3 && Header.PointDataFormatID > 5) {
			PointRecord.V11Synthetic = (uchar) (PointRecord.ClassificationFlags & 0x01);	// bits 0: 0 if point is from scan data, 1 if point was created by another method
			PointRecord.V11KeyPoint = (uchar) ((PointRecord.ClassificationFlags & 0x02) >> 1);	// bit 1: 0 if point is not a model key-point, 1 otherwise...key-points should not be removed by processing operations
			PointRecord.V11Withheld = (uchar) ((PointRecord.ClassificationFlags & 0x04) >> 2);	// bit 2: 0 if point is valid, 1 if point should not be included in processing (synonymous with deleted)
			PointRecord.V14Overlap = (uchar) ((PointRecord.ClassificationFlags & 0x08) >> 3);	// bit 3: Overlap flag...set if point is in overlap area of 2 or more swaths
		}
		else if (Header.Version > 1.0) {
			PointRecord.V11Classification = PointRecord.Classification & 0x1F;	// bits 0-4: classification code for return...corresponds to variable length record in CLASClassificationLookupTable
			PointRecord.V11Synthetic = (uchar) ((PointRecord.Classification & 0x20) >> 5);	// bit 5: 0 if point is from scan data, 1 if point was created by another method
			PointRecord.V11KeyPoint = (uchar) ((PointRecord.Classification & 0x40) >> 6);	// bit 6: 0 if point is not a model key-point, 1 otherwise...key-points should not be removed by processing operations
			PointRecord.V11Withheld = (uchar) ((PointRecord.Classification & 0x80) >> 7);	// bit 7: 0 if point is valid, 1 if point should not be included in processing (synonymous with deleted)
			PointRecord.V14Overlap = 0;
		}
		else {
			PointRecord.V11Classification = PointRecord.Classification;
			PointRecord.V11Synthetic = 0;
			PointRecord.V11KeyPoint = 0;
			PointRecord.V11Withheld = 0;
			PointRecord.V14Overlap = 0;
		}

		m_CurrentPointIndex ++;
	}
	return(retcode);
}

BOOL CLASPointDataRecord::ReadBaseValues(FILE *FileHandle)
{
	// read format 0 fields
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RawX, sizeof(RawX), 1, FileHandle);
	cnt += (int) fread(&RawY, sizeof(RawY), 1, FileHandle);
	cnt += (int) fread(&RawZ, sizeof(RawZ), 1, FileHandle);
	cnt += (int) fread(&Intensity, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&ReturnBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&Classification, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&ScanAngleRank, sizeof(char), 1, FileHandle);
	cnt += (int) fread(&FileMarker, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&UserBitField, sizeof(ushort), 1, FileHandle);		// Point Source ID in LAS version 1.2

	// unpack the return bit info into separate variables
	UnpackReturnBitInfo();

	if (cnt == 9)
		retcode = TRUE;

	return(retcode);
}

void CLASPointDataRecord::ScaleX(double Offset, double Scale)
{
	X = (RawX * Scale) + Offset;
}

void CLASPointDataRecord::ScaleY(double Offset, double Scale)
{
	Y = (RawY * Scale) + Offset;
}

void CLASPointDataRecord::ScaleZ(double Offset, double Scale)
{
	Z = (RawZ * Scale) + Offset;
}

void CLASFormatFile::Close()
{
	if (IsValid()) {
		if (m_FileHandle) {
			fclose(m_FileHandle);
			m_FileHandle = NULL;
		}
		m_FileHandle = NULL;
		m_ValidHeader = FALSE;
		m_CurrentPointIndex = -1;
	}
}

BOOL CLASFormatFile::TestVariableRecordType(unsigned short RecordID, int ValidTypes)
{
	int RecordType = UNKNOWNRECORDTYPE;

	// determine record type

	// georeferencing records...normal for point data
	if (RecordID == 34735 || RecordID == 34736 || RecordID == 34737)
		RecordType = PROJECTIONRECORDS;

	// georeferencing records...usually for raster data
	if (RecordID == 33922 || RecordID == 33550 || RecordID == 34264)
		RecordType = PROJECTIONRECORDS;

	// waveform data as EVLR
	if (RecordID == 65535)
		return(FALSE);
	else {
		// test type and return
		return((RecordType & ValidTypes) != 0);
	}
}

BOOL CLASFormatFile::JumpToVariableRecord(int index)
{
	CLASVariableLengthRecord VarRec;
	if (IsValid() && Header.NumberOfVariableLengthRecords && index < (int) Header.NumberOfVariableLengthRecords) {
		fseek(m_FileHandle, Header.HeaderSize, SEEK_SET);

		if (index) {
			for (int i = 0; i < index; i ++) {
				// read the record
				VarRec.Read(m_FileHandle);

				// jump to end of record data
				fseek(m_FileHandle, VarRec.RecordLengthAfterHeader, SEEK_CUR);
			}
		}
		return(TRUE);
	}
	return(FALSE);
}

int CLASFormatFile::CopyVariableRecord(int index, FILE *OutputFileHandle, int RecordTypes)
{
	CLASVariableLengthRecord VarRec;
	if (IsValid() && Header.NumberOfVariableLengthRecords && index < (int) Header.NumberOfVariableLengthRecords) {
		if (JumpToVariableRecord(index)) {
			// ready to copy record to OutputFileHandle
			unsigned char c;
			int ReadCnt = 0;
			int WriteCnt = 0;
			BOOL ReadGood = FALSE;
			BOOL WriteGood = FALSE;

			ReadGood = VarRec.Read(m_FileHandle);
			if (ReadGood) {
				// make sure this record is a type to copy
				if (TestVariableRecordType(VarRec.RecordID, RecordTypes)) {
					WriteGood = VarRec.WriteHeader(OutputFileHandle);

					// copy data for record
					if (WriteGood) {
						for (int i = 0; i < VarRec.RecordLengthAfterHeader; i ++) {
							ReadCnt += (int) fread(&c, sizeof(unsigned char), 1, m_FileHandle);
							WriteCnt += (int) fwrite(&c, sizeof(unsigned char), 1, OutputFileHandle);
						}
						
						if (ReadCnt != WriteCnt) {
							WriteGood = FALSE;
							WriteCnt = -(VLRHEADERSIZE + 1);			// forces return value of -1
						}
					}
					return(VLRHEADERSIZE + WriteCnt);
				}
				else
					return(0);		// not a record type we are copying
			}
			else
				return(-1);			// error...couldn't read record header
		}
		return(-1);					// error...couldn't jumpt to start of record
	}
	return(-1);						// error...LASFormatFile not valid or bad record index
}

BOOL CLASVariableLengthRecord::Read(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fread(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&RecordLengthAfterHeader, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(Description, sizeof(char), 32, FileHandle);

	// add terminators
	UserID[16] = '\0';
	Description[32] = '\0';

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASVariableLengthRecord::ReadHeader(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fread(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&RecordLengthAfterHeader, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(Description, sizeof(char), 32, FileHandle);

	// add terminators
	UserID[16] = '\0';
	Description[32] = '\0';

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASVariableLengthRecord::WriteHeader(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fwrite(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fwrite(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(&RecordLengthAfterHeader, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(Description, sizeof(char), 32, FileHandle);

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASExtendedVariableLengthRecord::Read(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fread(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&RecordLengthAfterHeader, sizeof(unsigned __int64), 1, FileHandle);
	cnt += (int) fread(Description, sizeof(char), 32, FileHandle);

	// add terminators
	UserID[16] = '\0';
	Description[32] = '\0';

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASExtendedVariableLengthRecord::ReadHeader(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fread(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&RecordLengthAfterHeader, sizeof(unsigned __int64), 1, FileHandle);
	cnt += (int) fread(Description, sizeof(char), 32, FileHandle);

	// add terminators
	UserID[16] = '\0';
	Description[32] = '\0';

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASExtendedVariableLengthRecord::WriteHeader(FILE *FileHandle)
{
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fwrite(&RecordSignature, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(UserID, sizeof(char), 16, FileHandle);
	cnt += (int) fwrite(&RecordID, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(&RecordLengthAfterHeader, sizeof(unsigned __int64), 1, FileHandle);
	cnt += (int) fwrite(Description, sizeof(char), 32, FileHandle);

	if (cnt == 51)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASPointDataRecord::WriteBaseValues(FILE *FileHandle)
{
	// read format 0 fields
	BOOL retcode = FALSE;

	// pack the return bit info into separate variables
	PackReturnBitInfo();

	int cnt = 0;
	cnt += (int) fwrite(&RawX, sizeof(RawX), 1, FileHandle);
	cnt += (int) fwrite(&RawY, sizeof(RawY), 1, FileHandle);
	cnt += (int) fwrite(&RawZ, sizeof(RawZ), 1, FileHandle);
	cnt += (int) fwrite(&Intensity, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(&ReturnBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&Classification, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&ScanAngleRank, sizeof(char), 1, FileHandle);
	cnt += (int) fwrite(&FileMarker, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&UserBitField, sizeof(ushort), 1, FileHandle);

	if (cnt == 9)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASPointDataRecord::Write(FILE *FileHandle)
{
	return(WriteBaseValues(FileHandle));
}

void CLASPointDataRecordFormatAll::UnpackReturnBitInfo14plus()
{
	ReturnNumber = ReturnBitInfo & 0x0F;
	NumberOfReturns = ((ReturnBitInfo & 0xF0) >> 4);
}

void CLASPointDataRecordFormatAll::PackReturnBitInfo14plus()
{
	ReturnBitInfo = 0;
	ReturnBitInfo |= (ReturnNumber & 0x0F);
	ReturnBitInfo |= ((NumberOfReturns & 0x0F) << 4);
}

void CLASPointDataRecordFormatAll::UnpackClassificationBitInfo14plus()
{
	ClassificationFlags = ClassificationBitInfo & 0x0F;
	ScannerChannel = (ClassificationBitInfo & 0x30) >> 4;
	ScanDirectionFlag = (ClassificationBitInfo & 0x40) >> 6;
	EdgeOfFlightLineFlag = (ClassificationBitInfo & 0x80) >> 7;
}

void CLASPointDataRecordFormatAll::PackClassificationBitInfo14plus()
{
	ClassificationBitInfo = 0;
	ClassificationBitInfo |= (ClassificationFlags & 0x0F);
	ClassificationBitInfo |= ((ScannerChannel & 0x03) << 4);
	ClassificationBitInfo |= ((ScanDirectionFlag & 0x01) << 6);
	ClassificationBitInfo |= ((EdgeOfFlightLineFlag & 0x01) << 7);
}

BOOL CLASPointDataRecordFormatAll::Write(FILE *FileHandle, int Format)
{
	if (Format > 5) {
		if (WriteBaseValues14plus(FileHandle)) {
			if (WriteAdditionalValues14plus(FileHandle, Format)) {
				return(WriteExtraBytes(FileHandle) == ExtraByteCount);
			}
		}
	}
	else {
		if (CLASPointDataRecord::Write(FileHandle)) {
			if (WriteAdditionalValues(FileHandle, Format)) {
				return(WriteExtraBytes(FileHandle) == ExtraByteCount);
			}
		}
	}
	return(FALSE);
}

BOOL CLASPointDataRecordFormatAll::WriteBaseValues14plus(FILE *FileHandle)
{
	// read format 0 fields
	BOOL retcode = FALSE;

	// pack the return bit info into a single variables
	PackReturnBitInfo14plus();

	// pack the classification bit info into a single variables
	PackClassificationBitInfo14plus();

	int cnt = 0;
	cnt += (int) fwrite(&RawX, sizeof(RawX), 1, FileHandle);
	cnt += (int) fwrite(&RawY, sizeof(RawY), 1, FileHandle);
	cnt += (int) fwrite(&RawZ, sizeof(RawZ), 1, FileHandle);
	cnt += (int) fwrite(&Intensity, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(&ReturnBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&ClassificationBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&Classification, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&FileMarker, sizeof(uchar), 1, FileHandle);
	cnt += (int) fwrite(&ScanAngle, sizeof(short), 1, FileHandle);
	cnt += (int) fwrite(&UserBitField, sizeof(ushort), 1, FileHandle);
	cnt += (int) fwrite(&GPSTime, sizeof(double), 1, FileHandle);

	if (cnt == 11)
		retcode = TRUE;

	return(retcode);
}

BOOL CLASPointDataRecordFormatAll::ReadBaseValues14plus(FILE *FileHandle)
{
	// read format 0 fields
	BOOL retcode = FALSE;

	int cnt = 0;
	cnt += (int) fread(&RawX, sizeof(RawX), 1, FileHandle);
	cnt += (int) fread(&RawY, sizeof(RawY), 1, FileHandle);
	cnt += (int) fread(&RawZ, sizeof(RawZ), 1, FileHandle);
	cnt += (int) fread(&Intensity, sizeof(ushort), 1, FileHandle);
	cnt += (int) fread(&ReturnBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&ClassificationBitInfo, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&Classification, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&FileMarker, sizeof(uchar), 1, FileHandle);
	cnt += (int) fread(&ScanAngle, sizeof(short), 1, FileHandle);
	cnt += (int) fread(&UserBitField, sizeof(ushort), 1, FileHandle);		// Point Source ID in LAS version 1.2
	cnt += (int) fread(&GPSTime, sizeof(double), 1, FileHandle);

	// unpack the return byte info into separate variables
	UnpackReturnBitInfo14plus();

	// unpack the classification byte info into separate variables
	UnpackClassificationBitInfo14plus();

	if (cnt == 11)
		retcode = TRUE;

	return(retcode);
}

CLASPointDataRecordFormatAll& CLASPointDataRecordFormatAll::operator=(CLASPointDataRecordFormatAll& src)
{
	X = src.X;
	Y = src.Y;
	Z = src.Z;
	Intensity = src.Intensity;
	ReturnBitInfo = src.ReturnBitInfo;
	ClassificationBitInfo = src.ClassificationBitInfo;
	Classification = src.Classification;
	ClassificationFlags = src.ClassificationFlags;
	ScanAngleRank = src.ScanAngleRank;
	ScanAngle = src.ScanAngle;
	FileMarker = src.FileMarker;
	UserBitField = src.UserBitField;
	ReturnNumber = src.ReturnNumber;
	NumberOfReturns = src.NumberOfReturns;
	ScanDirectionFlag = src.ScanDirectionFlag;
	ScannerChannel = src.ScannerChannel;
	EdgeOfFlightLineFlag = src.EdgeOfFlightLineFlag;
	V11Classification = src.V11Classification;
	V11Synthetic = src.V11Synthetic;
	V11KeyPoint = src.V11KeyPoint;
	V11Withheld = src.V11Withheld;
	GPSTime = src.GPSTime;
	Red = src.Red;
	Green = src.Green;
	Blue = src.Blue;
	NIR = src.NIR;

	return *this;
}

BOOL CLASPointDataRecordFormatAll::WriteAdditionalValues(FILE *FileHandle, int Format)
{
	int count = 0;
	int targetcount = 0;

	// write GPS time for format 1, 3, 4, and 5 records
	if (Format == 1 || Format == 3 || Format == 4 || Format == 5) {
		count += (int) fwrite(&GPSTime, sizeof(double), 1, FileHandle);
		targetcount ++;
	}

	// write RGB values for format 2, 3, and 5
	if (Format == 2 || Format == 3 || Format == 5) {
		count += (int) fwrite(&Red, sizeof(ushort), 1, FileHandle);
		count += (int) fwrite(&Green, sizeof(ushort), 1, FileHandle);
		count += (int) fwrite(&Blue, sizeof(ushort), 1, FileHandle);
		targetcount += 3;
	}

	// write waveform packet info...writing is more complex than reading since you have to know the offset
	// @@@@ I am pretty sure that writing of point records 4 and 5 will fail unless you are writing the entire file
	// @@@@ The waveform data will not match the point records if only some of the returns are being written. However,
	// @@@@ the entire variable length record with the waveform packets will be copied to an output file so the waveform
	// @@@@ may be correct for the returns that are in the output file but you will have all of the extra waveform
	// @@@@ packets for returns that are not included in the output file.
	if (Format == 4 || Format == 5) {
		count += (int) fwrite(&WavePacketDescriptorIndex, sizeof(uchar), 1, FileHandle);
		count += (int) fwrite(&ByteOffsetToWaveformData, sizeof(unsigned __int64), 1, FileHandle);
		count += (int) fwrite(&WaveformPacketSizeInBytes, sizeof(WaveformPacketSizeInBytes), 1, FileHandle);
		count += (int) fwrite(&ReturnPointWaveformLocation, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Xt, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Yt, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Zt, sizeof(float), 1, FileHandle);
		targetcount += 7;
	}

	return(count == targetcount);
}

BOOL CLASPointDataRecordFormatAll::WriteAdditionalValues14plus(FILE *FileHandle, int Format)
{
	int count = 0;
	int targetcount = 0;

	// write RGB values for format 2, 3, and 5
	if (Format == 7 || Format == 8 || Format == 10) {
		count += (int) fwrite(&Red, sizeof(ushort), 1, FileHandle);
		count += (int) fwrite(&Green, sizeof(ushort), 1, FileHandle);
		count += (int) fwrite(&Blue, sizeof(ushort), 1, FileHandle);
		targetcount += 3;

		if (Format == 8 || Format == 10) {
			count += (int) fwrite(&NIR, sizeof(ushort), 1, FileHandle);
			targetcount += 1;
		}
	}

	// write waveform packet info...writing is more complex than reading since you have to know the offset
	// @@@@ I am pretty sure that writing of point records 4 and 5 will fail unless you are writing the entire file
	// @@@@ The waveform data will not match the point records if only some of the returns are being written. However,
	// @@@@ the entire variable length record with the waveform packets will be copied to an output file so the waveform
	// @@@@ may be correct for the returns that are in the output file but you will have all of the extra waveform
	// @@@@ packets for returns that are not included in the output file.
	if (Format == 9 || Format == 10) {
		count += (int) fwrite(&WavePacketDescriptorIndex, sizeof(uchar), 1, FileHandle);
		count += (int) fwrite(&ByteOffsetToWaveformData, sizeof(unsigned __int64), 1, FileHandle);
		count += (int) fwrite(&WaveformPacketSizeInBytes, sizeof(WaveformPacketSizeInBytes), 1, FileHandle);
		count += (int) fwrite(&ReturnPointWaveformLocation, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Xt, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Yt, sizeof(float), 1, FileHandle);
		count += (int) fwrite(&Zt, sizeof(float), 1, FileHandle);
		targetcount += 7;
	}

	return(count == targetcount);
}

BOOL CLASPointDataRecordFormatAll::ReadAdditionalValues(FILE *FileHandle, int Format)
{
	int count = 0;
	int targetcount = 0;

	// read GPS time for format 1, 3, 4, and 5 records
	if (Format == 1 || Format == 3 || Format == 4 || Format == 5) {
		count += (int) fread(&GPSTime, sizeof(double), 1, FileHandle);
		targetcount ++;
	}

	// read RGB values for format 2, 3, and 5
	if (Format == 2 || Format == 3 || Format == 5) {
		count += (int) fread(&Red, sizeof(ushort), 1, FileHandle);
		count += (int) fread(&Green, sizeof(ushort), 1, FileHandle);
		count += (int) fread(&Blue, sizeof(ushort), 1, FileHandle);
		targetcount += 3;
	}

	// read waveform packet info
	if (Format == 4 || Format == 5) {
		count += (int) fread(&WavePacketDescriptorIndex, sizeof(uchar), 1, FileHandle);
		count += (int) fread(&ByteOffsetToWaveformData, sizeof(unsigned __int64), 1, FileHandle);
		count += (int) fread(&WaveformPacketSizeInBytes, sizeof(WaveformPacketSizeInBytes), 1, FileHandle);
		count += (int) fread(&ReturnPointWaveformLocation, sizeof(float), 1, FileHandle);
		count += (int) fread(&Xt, sizeof(float), 1, FileHandle);
		count += (int) fread(&Yt, sizeof(float), 1, FileHandle);
		count += (int) fread(&Zt, sizeof(float), 1, FileHandle);
		targetcount += 7;
	}

	return(count == targetcount);
}

BOOL CLASPointDataRecordFormatAll::ReadAdditionalValues14plus(FILE *FileHandle, int Format)
{
	int count = 0;
	int targetcount = 0;

	// read RGB values for format 7 and 8
	if (Format == 7 || Format == 8 || Format == 10) {
		count += (int) fread(&Red, sizeof(ushort), 1, FileHandle);
		count += (int) fread(&Green, sizeof(ushort), 1, FileHandle);
		count += (int) fread(&Blue, sizeof(ushort), 1, FileHandle);
		targetcount += 3;

		if (Format == 8 || Format == 10) {
			count += (int) fread(&NIR, sizeof(ushort), 1, FileHandle);
			targetcount += 1;
		}
	}

	// read waveform packet info
	if (Format == 9 || Format == 10) {
		count += (int) fread(&WavePacketDescriptorIndex, sizeof(uchar), 1, FileHandle);
		count += (int) fread(&ByteOffsetToWaveformData, sizeof(unsigned __int64), 1, FileHandle);
		count += (int) fread(&WaveformPacketSizeInBytes, sizeof(WaveformPacketSizeInBytes), 1, FileHandle);
		count += (int) fread(&ReturnPointWaveformLocation, sizeof(float), 1, FileHandle);
		count += (int) fread(&Xt, sizeof(float), 1, FileHandle);
		count += (int) fread(&Yt, sizeof(float), 1, FileHandle);
		count += (int) fread(&Zt, sizeof(float), 1, FileHandle);
		targetcount += 7;
	}

	return(count == targetcount);
}

int CLASPointDataRecord::ReadExtraBytes(FILE *FileHandle, int nbytes)
{
	ExtraByteCount = nbytes;
	return((int) fread(ExtraBytes, sizeof(uchar), nbytes, FileHandle));
}

int CLASPointDataRecord::WriteExtraBytes(FILE *FileHandle)
{
	if (ExtraByteCount == 0)
		return(0);

	return((int) fwrite(ExtraBytes, sizeof(uchar), ExtraByteCount, FileHandle));
}
