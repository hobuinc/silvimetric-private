// LASFormatFile.h: interface for the CLASFormatFile class.
//
//////////////////////////////////////////////////////////////////////
//
// 1/17/2012
// modified the logic used to open LAS files so I can assign a larger buffer for reading point data. in use, the
// buffer is managed by the calling code. previous versions tried to set a larger buffer but the buffer was only 
// being set whe the file was opened to determine the file format and not when reading point data. The larger buffer
// doesn't seem to make much difference in the initial tests.
//
// 7/19/2013
// changed LAS public header initial value for OffsetToData form 1026 to HEADERSIZE
//
// 9/26/2013
// added () to bit shift when extracting information from classification field in ReadNextPoint(). Shift was being applied to the
// mask instead of the masked value
//
// 12/22/2016
// Made some changes to accomodate LAS V1.4. These mainly involved adding extra values for the header and reading and writing them.
// There are new point record types in V1.4. The new types aren't much different from the original point types but varialbe types are 
// different so there needs to be new logic/variables to handle the new variables. The big change in V1.4 is the possibility of many
// more points in the file so teh V1.4 header has the original variables for the number of point records and the number of points
// by type that are 4-byte integars but adds additional counts that are 8-byte integers at the end of the header. This change breaks 
// all of the original FUSION indexing (actually just having files larger than UINT_MAX bytes (so the offsets to point locations
// can't be stored as a 4-byte integer).
//
// I expect to make the changes to read all the new point record formats but am wondering if it isn't time for a major redesign of
// the logic used to read point data. The goal would be to design the logic around LAS from the start (my current design started off
// using generic ASCII formats), to make changes to the indexing (look at LAStools indexing), and implement a point data manager to
// facilitate working with large files and multi-file sets.
//
// 5/4/2018
// More changes to deal with LAS 1.4 data with more than 7 returns per pulse. Had to fix the way I was reading/writing the pulse number and
// number of returns in the pulse to use the "extended" variables in the headers and point records (in laszip.dll)
//
// 10/10/2018
// Modified the behavior when writing headers for V1.4 LAS files to maintain legacy compatibility. Previously, I was setting the legacy point
// count and return count array to 0 when the version was 1.4+. The correct behavior according the LAS specification is to maintain legacy
// compatibility if possible (not a requirement) when using point record formats < 6 and fewer than ULONG_MAX total points.
//
// Also made a change to use the VLRHEADERSIZE constant when copying VLRs from one file to another. Previous code had some values hard-coded.
//

#if !defined(AFX_LASFORMATFILE_H__8A7C6B08_13EB_46AE_A854_B37E90F3FB09__INCLUDED_)
#define AFX_LASFORMATFILE_H__8A7C6B08_13EB_46AE_A854_B37E90F3FB09__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <cstdint>

#define		HEADERSIZE			227
#define		HEADERSIZEV12		227
#define		HEADERSIZEV13		235
#define		HEADERSIZEV14		375

#define		VLRHEADERSIZE		54

// record types for CopyVariableRecord()
#define		ALLRECORDS			0x7FFF
#define		PROJECTIONRECORDS	0x01
#define		UNKNOWNRECORDTYPE	0x40

// LAS is a binary format with explicitly sized fields.  FUSION was written
// for Windows, where long is 32 bits even in 64-bit builds.  Do not use the
// platform's long here: it is 64 bits on macOS and most 64-bit Linux targets.
typedef  std::uint32_t fusion_ulong;
typedef  unsigned short ushort;
typedef  unsigned char uchar;
typedef	 unsigned char bitbyte;

typedef  struct {
	unsigned char	ClassNumber;					// value used in point records
	char			Description[16];				// description of class
} CLASSIFICATION;

typedef  struct {
	unsigned char	FileMarkerNumber;				// value used in point records
	char			FileName[257];					// description of flightline
} FLIGHTLINE;

typedef struct {
	ushort			FileSourceID;					// flight line number or other group indicator
	ushort			Reserved;						// extra bytes...global encoding in LAS 1.4
} V11FILESOURCEID;

union FileSrcID {
	V11FILESOURCEID V11;							// version 1.1 file source ID and reserved block
	fusion_ulong			Reserved;						// original version 1.0 data block
};

class CLASPublicHeaderBlock
{
public:
	char			FileSignature[5];				// must be "LASF"...magic number like signature
	FileSrcID		Reserved;						// extra variable...this has different interpretations depending on the LAS version
	fusion_ulong			GUIDData1;						// Globally unique identifier
	ushort			GUIDData2;						// Globally unique identifier
	ushort			GUIDData3;						// Globally unique identifier
	uchar			GUIDData4[9];					// Globally unique identifier
	uchar			VersionMajor;					// left hand side of version number
	uchar			VersionMinor;					// right hand side of version number
	char			SystemIdentifier[33];			// LIDAR hardware identifier
	char			GeneratingSoftware[33];			// generating software identifier
	ushort			FlightDateJulian;				// julian day of the year the data was collected
	ushort			Year;							// year the data was collected
	ushort			HeaderSize;						// actual size of the header block
	fusion_ulong			OffsetToData;					// number of bytes from beginning of file to the actual point data
	fusion_ulong			NumberOfVariableLengthRecords;	// number of variable length records
	uchar			PointDataFormatID;				// format identifier for point data records
	ushort			PointDataRecordLength;			// length of each point record
	fusion_ulong			LegacyNumberOfPointRecords;		// total number of point records in file...in LAS 1.4 this should be 0
	fusion_ulong			LegacyNumberOfPointsByReturn[5];// array containing number of point records per return...first value is number of first returns...in LAS 1.4 all should be 0
	double			XScaleFactor;					// x scale factor
	double			YScaleFactor;					// y scale factor
	double			ZScaleFactor;					// z scale factor
	double			XOffset;						// x offset
	double			YOffset;						// y offset
	double			ZOffset;						// z offset
	double			MaxX;							// maximum x value (after expansion using scale and offset)
	double			MinX;							// minimum x value (after expansion using scale and offset)
	double			MaxY;							// maximum y value (after expansion using scale and offset)
	double			MinY;							// minimum y value (after expansion using scale and offset)
	double			MaxZ;							// maximum z value (after expansion using scale and offset)
	double			MinZ;							// minimum z value (after expansion using scale and offset)
	unsigned __int64 WaveformStart;					// Start of waveform data packet record...version 1.3+ only

	unsigned __int64 StartOfExtendedVLR;			// start of first extended VLR...version 1.4+ only
	fusion_ulong			NumberOfExtendedVLRs;			// number of extended VLRs...version 1.4+ only
	unsigned __int64 ExtendedNumberOfPointRecords;	// extended number of point records...version 1.4+ only
	unsigned __int64 ExtendedNumberOfPointsByReturn[15];	// extended number of points by return...version 1.4+ only

	unsigned __int64 NumberOfPointRecords;			// total number of point records in file...in LAS 1.4 this should be 0
	unsigned __int64 NumberOfPointsByReturn[15];		// array containing number of point records per return...first value is number of first returns...in LAS 1.4 all should be 0

	unsigned long UserDataInHeaderSize;				// computed using expected header size and actual header size (HeaderSize)
	unsigned char* UserDataInHeader;				// extra data included as part of LAS file header

	unsigned long UserDataAfterHeaderSize;			// actual header size (HeaderSize) + VLR size and offset to point data
	unsigned char* UserDataAfterHeader;				// extra data included after header but before point data

	float			Version;						// combination of VersionMajor.VersionMinor
	ushort			FileSourceID;					// version 1.1 flight line number of other grouping code
public:
	BOOL Write(FILE *FileHandle);
	BOOL Read(FILE* FileHandle);
	CLASPublicHeaderBlock();
	virtual ~CLASPublicHeaderBlock();
};

class CLASVariableLengthRecord
{
public:
	ushort			RecordSignature;				// record signature for variable length record...0xAABB
	char			UserID[17];						// ASCII user identifier for creator of variable length record
	ushort			RecordID;						// record identifier
	ushort			RecordLengthAfterHeader;		// number of bytes in record after the standard portion of th header
	char			Description[33];				// record description

public:
	BOOL Read(FILE* FileHandle);					// poorly named...actually only reads the header fields
	BOOL ReadHeader(FILE* FileHandle);
	BOOL WriteHeader(FILE *FileHandle);
	CLASVariableLengthRecord();
	virtual ~CLASVariableLengthRecord();
};

class CLASExtendedVariableLengthRecord
{
public:
	ushort				RecordSignature;				// record signature for variable length record...0xAABB
	char				UserID[17];						// ASCII user identifier for creator of variable length record
	ushort				RecordID;						// record identifier
	unsigned __int64	RecordLengthAfterHeader;		// number of bytes in record after the standard portion of th header
	char				Description[33];				// record description

public:
	BOOL Read(FILE* FileHandle);					// poorly named...actually only reads the header fields
	BOOL ReadHeader(FILE* FileHandle);
	BOOL WriteHeader(FILE *FileHandle);
	CLASExtendedVariableLengthRecord();
	virtual ~CLASExtendedVariableLengthRecord();
};

class CLASPointDataRecord
{
public:
	std::int32_t	RawX;							// return position
	std::int32_t	RawY;							// return position
	std::int32_t	RawZ;							// return position
	double			X;								// return position scaled using header values
	double			Y;								// return position scaled using header values
	double			Z;								// return position scaled using header values
	ushort			Intensity;						// pulse return magnitude...system specific with unknown range
	bitbyte			ReturnBitInfo;					// bit field containing return number, # returns, scan direction flag and edge flag
	uchar			Classification;					// classification code for return...corresponds to variable length record in CLASClassificationLookupTable
	char			ScanAngleRank;					// pulse angle including aircraft roll...0 is NADIR (-) left (+) right
	uchar			FileMarker;						// file marker code...corresponds to variable length record in CLASFlightLineHeaderLookupTable...version 1.1+ called User Data
	ushort			UserBitField;					// user bit field...version 1.1+ this is point source ID and it corresponds to the File Source ID in the public header
													// for files containing multiple flightlines, this would presumably indicate the flight line for the point

	uchar			ReturnNumber;					// pulse return number for a given pulse
	uchar			NumberOfReturns;				// number of returns for the pulse
	uchar			ScanDirectionFlag;				// scan direction...1 indicates positive scan direction, 0 negative
	uchar			EdgeOfFlightLineFlag;			// flag indicating pulse is on edge of filght line...last pulse in scan line before changing direction

// need to add overlap flag to accomodate V1.4 point records 6+...it looks like I messed this up when making changes for V1.4 and added the V11Overlap variable tied to bit 3
// in the "classification flags" probably want to have this OverlapFlag variable and then set it to 0 for <V1.4 and point records < 6

	// version 1.1+ classification code variables...bit values change depending on LAS file version
	uchar			V11Classification;				// bits 0-4: classification code for return...corresponds to variable length record in CLASClassificationLookupTable
	uchar			V11Synthetic;					// bit 5: 0 if point is from scan data, 1 if point was created by another method
	uchar			V11KeyPoint;					// bit 6: 0 if point is not a model key-point, 1 otherwise...key-points should not be removed by processing operations
	uchar			V11Withheld;					// bit 7: 0 if point is valid, 1 if point should not be included in processing (synonymous with deleted)

	// version 1.4+ classification flags have the synthetic, keypoint, and withheld bits plus an overlap bit
	uchar			V14Overlap;						// bit 3: Overlap flag...set if point is in overlap area of 2 or more swaths

	uchar			ExtraBytes[1024];				// container for extra bytes with each return
	int				ExtraByteCount;					// number of extra bytes...stored in LAS file class but needed when writing points

public:
	int WriteExtraBytes(FILE *FileHandle);
	int ReadExtraBytes(FILE *FileHandle, int nbytes);
	virtual BOOL Write(FILE* FileHandle);
	BOOL WriteBaseValues(FILE* FileHandle);

	void ScaleX(double Offset, double Scale);
	void ScaleY(double Offset, double Scale);
	void ScaleZ(double Offset, double Scale);
	BOOL ReadBaseValues(FILE* FileHandle);

	CLASPointDataRecord();
	virtual ~CLASPointDataRecord();

	void UnpackReturnBitInfo(BOOL FixFormatError = FALSE);
	void PackReturnBitInfo();
};

class CLASPointDataRecordFormat1 : public CLASPointDataRecord
{
public:
	double			GPSTime;						// GPS time at which the point was acquired

public:
	BOOL WriteAdditionalValues(FILE* FileHandle);
	BOOL Write(FILE* FileHandle);
	BOOL ReadAdditionalValues(FILE* FileHandle);
	CLASPointDataRecordFormat1();
	virtual ~CLASPointDataRecordFormat1();
};

class CLASPointDataRecordFormatAll : public CLASPointDataRecord
{
public:
	bitbyte			ClassificationBitInfo;			// bit field containing classification, scanner channel, scan direction flag and edge flag
	uchar			ClassificationFlags;			// classification flags for return
	uchar			ScannerChannel;					// scanner channel
	short			ScanAngle;						// scan angle...scaled by 0.006 degrees (ScanAngle * 0.006 = angle in degrees)
	double			GPSTime;						// GPS time at which the point was acquired
	ushort			Red;							// Red image channel value...always scaled to 16-bit value...divide by 256 to get 8-bit value
	ushort			Green;							// Green image channel value...always scaled to 16-bit value...divide by 256 to get 8-bit value
	ushort			Blue;							// Blue image channel value...always scaled to 16-bit value...divide by 256 to get 8-bit value
	ushort			NIR;							// Near infrared channel value...always scaled to 16-bit value...divide by 256 to get 8-bit value
	uchar			WavePacketDescriptorIndex;		// index of user defined record describing the waveform packet associated with the lidar point
	unsigned __int64 ByteOffsetToWaveformData;		// offseet to start of waveform data packet within the waveform variable length record
	std::uint32_t	WaveformPacketSizeInBytes;		// size in bytes of the waveform packet associated with this return
	float			ReturnPointWaveformLocation;	// offset in picoseconds from the first digitized value to the location of the location within the waveform packet where this return was detected
	float			Xt;								// coefficient for the parametric line equation for interpolating points along the waveform
	float			Yt;								// coefficient for the parametric line equation for interpolating points along the waveform
	float			Zt;								// coefficient for the parametric line equation for interpolating points along the waveform

public:
//	CLASPointDataRecordFormatAll& CLASPointDataRecordFormatAll::operator=(CLASPointDataRecordFormatAll& src);
	CLASPointDataRecordFormatAll& operator=(CLASPointDataRecordFormatAll& src);
	BOOL WriteAdditionalValues(FILE* FileHandle, int Format = 0);
	BOOL Write(FILE* FileHandle, int Format = 0);
	BOOL ReadAdditionalValues(FILE* FileHandle, int Format = 0);
	CLASPointDataRecordFormatAll();
	virtual ~CLASPointDataRecordFormatAll();

	// functions to support LAS V1.4+
	BOOL WriteBaseValues14plus(FILE* FileHandle);
	BOOL ReadBaseValues14plus(FILE* FileHandle);
	BOOL WriteAdditionalValues14plus(FILE* FileHandle, int Format = 0);
	BOOL ReadAdditionalValues14plus(FILE* FileHandle, int Format = 0);
	void UnpackReturnBitInfo14plus();
	void UnpackClassificationBitInfo14plus();
	void PackReturnBitInfo14plus();
	void PackClassificationBitInfo14plus();
};

class CLASClassificationLookupTable : public CLASVariableLengthRecord
{
public:
	CLASSIFICATION* Class;						// classification code definitions
//	CLASSIFICATION Class[256];						// classification code definitions

public:
	CLASClassificationLookupTable();
	virtual ~CLASClassificationLookupTable();
};

class CLASFlightLineHeaderLookupTable : public CLASVariableLengthRecord
{
public:
	FLIGHTLINE*		FlightLine;				// flight line info
//	FLIGHTLINE		FlightLine[256];				// flight line info
public:
	CLASFlightLineHeaderLookupTable();
	virtual ~CLASFlightLineHeaderLookupTable();
};

class CLASHistogram : public CLASVariableLengthRecord
{
public:
	CLASHistogram();
	virtual ~CLASHistogram();
};

class CLASTextAreaDescription : public CLASVariableLengthRecord
{
public:
	CLASTextAreaDescription();
	virtual ~CLASTextAreaDescription();
};

// class to abstract LAS file...contains instances of most of the above objects
class CLASFormatFile  
{
public:
	CLASPublicHeaderBlock Header;
	CLASVariableLengthRecord VariableHeader;
//	CLASPointDataRecord PointRecordFormat0;
//	CLASPointDataRecordFormat1 PointRecordFormat1;
// 8/25/2010	CLASPointDataRecordFormat1 PointRecord;
	CLASPointDataRecordFormatAll PointRecord;
	CLASClassificationLookupTable ClassificationCodes;
	CLASFlightLineHeaderLookupTable FlightLineInfo;
	CLASTextAreaDescription FileDescription;
	int PointFormatRecordLength[32];
	int ExtraByteCount;
	FILE* m_FileHandle;

public:
	BOOL TestVariableRecordType(unsigned short RecordID, int ValidTypes);
	BOOL JumpToVariableRecord(int index = 0);
	int CopyVariableRecord(int index, FILE *FileHandle, int RecordTypes = ALLRECORDS);
	void Close();
	BOOL ReadNextPoint();
	BOOL IsValid();
	void JumpToPointRecord(int index = 0);
	BOOL Open(LPCTSTR FileName, char *buffer = NULL, int buffersize = 0);
	BOOL VerifyFormat(LPCTSTR FileName);
	CString m_FileName;
	CLASFormatFile();
	virtual ~CLASFormatFile();

private:
	unsigned __int64  m_CurrentPointIndex;
	BOOL m_ValidHeader;
};

#endif // !defined(AFX_LASFORMATFILE_H__8A7C6B08_13EB_46AE_A854_B37E90F3FB09__INCLUDED_)
