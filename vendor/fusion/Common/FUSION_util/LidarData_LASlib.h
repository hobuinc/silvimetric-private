// LidarData.h: interface for the CLidarData class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_LIDARDATA_H__DCBA98E0_2288_11D4_A124_A80F08C10B02__INCLUDED_)
#define AFX_LIDARDATA_H__DCBA98E0_2288_11D4_A124_A80F08C10B02__INCLUDED_

// defines to maintain compatibility with updated LASzip.dll
#define number_of_returns_of_given_pulse number_of_returns
#define extended_number_of_returns_of_given_pulse extended_number_of_returns

#include <cstdint>

#include "DataFile.h"	// Added by ClassView
#include "COPCIndex.h"
#include "SpatialExtent.h"

#ifdef USE_LASLIB
#pragma message("*****Using LASlib for LAS access...")

#include "lasreader.hpp"
#include "laswriter.hpp"
#else
#pragma message("*****Using PNW code and/or LASzip DLL for LAS access...")

#include "laszip_api.h"
#include "LASFormatFile.h"
#endif

// type definitions (shortened for ease of use)
typedef  std::uint32_t ulong;
typedef  unsigned short ushort;
typedef  unsigned char uchar;
typedef	 unsigned char bitbyte;

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// save current structure packing value and set to 4 so LIDARRETURN structure doesn't have any extra padding
#pragma pack(push, before_lidardata, 4)

#define		INVALID			0
#define		ASCIIDATA		1
#define		BINDATA			2
#define		LASDATA			3


// reason why a file might be invalid
#define		NOATTEMPTTOOPEN		100
#define		BADFORMAT			101
#define		NOTEXIST			102
#define		LAZNODLL			103
#define		CANTOPEN			104
#define		NODATA				105
#define		BADCOPCINDEX		106

#define		NOFILEERROR			198
#define		UNKNOWN				199

// function is not part of the CLidarData class but is used to copy EVLRs from one file to another
BOOL CopyEVLRs(LPCTSTR SourceFileName, LPCTSTR DestinationFileName, int RecordTypes = ALLRECORDS);

// ATTENTION.....
// I screwed up the original data conversion and switched Nadir angle and intensity when converting from ASCII text to LDA formats
// original order in structure was intensity then nadir
typedef struct {
	int PulseNumber;
	int ReturnNumber;
	double X;
	double Y;
	float Elevation;
	float NadirAngle;
	float Intensity;
} LIDARRETURN;

// structure for additional LAS info
typedef struct {
	unsigned char	NumberOfReturns;				// number of returns for the pulse
	unsigned char	ScanDirectionFlag;				// scan direction...1 indicates positive scan direction, 0 negative
	unsigned char	EdgeOfFlightLineFlag;			// flag indicating pulse is on edge of filght line...last pulse in scan line before changing direction
	uchar			V11Classification;				// bits 0-4: classification code for return...corresponds to variable length record in CLASClassificationLookupTable
	uchar			V11Synthetic;					// bit 5: 0 if point is from scan data, 1 if point was created by another method
	uchar			V11KeyPoint;					// bit 6: 0 if point is not a model key-point, 1 otherwise...key-points should not be removed by processing operations
	uchar			V11Withheld;					// bit 7: 0 if point is valid, 1 if point should not be included in processing (synonymous with deleted)
	uchar			V14Overlap;						// bit 3 in classification flags for point format 6+: 0 if point is not in overlap area, 1 if in overlap area
	uchar			UserData;						// file marker code...corresponds to variable length record in CLASFlightLineHeaderLookupTable
	ushort			PointSourceID;					// user bit field...version 1.1+ this is point source ID and it corresponds to the File Source ID in the public header
	double			GPSTime;						// GPS time at which the point was acquired
} LAS_RETURNFIELDS;

class CLidarData  
{
public:
	BOOL LastPointIsSynthetic();
	BOOL LastPointIsKeypoint();
	BOOL LastPointIsWithheld();
	BOOL LastPointIsOverlap();
	void GetErrorMessage(CString& Err);
	int m_ReasonForInvalidFile;
	BOOL IsCompressed();
	CString m_FileName;
	BOOL PopulateLASzipHeader(laszip_header* LASzip_header, CLASPublicHeaderBlock *OldLASHeader);
	BOOL UpdateLASHeader(LPCTSTR FileName, BOOL UpdateExtent, BOOL UpdateOffsets, BOOL UpdateReturnCounts, BOOL UpdateTotalReturns, double minx, double miny, double minz, double maxx, double maxy, double maxz, double offsetx, double offsety, double offsetz, unsigned long R1, unsigned long R2, unsigned long R3, unsigned long R4, unsigned long R5, unsigned long TotalReturns);
	BOOL UpdateLASHeader(LPCTSTR FileName, BOOL UpdateExtent, BOOL UpdateOffsets, BOOL UpdateReturnCounts, BOOL UpdateTotalReturns, double minx, double miny, double minz, double maxx, double maxy, double maxz, double offsetx, double offsety, double offsetz, unsigned __int64* Rn, unsigned __int64 TotalReturns);
	BOOL m_HaveLASZIP_DLL;
	BOOL CheckForLASZIP_DLL();
	int GetFileFormat();

	BOOL ConvertToBinary(LPCTSTR OutputFileName, BOOL CreateIndex = FALSE, BOOL DecimateByPulse = FALSE);
	BOOL ConvertToASCII(LPCTSTR OutputFileName, int Format = 0, int StartRecord = 0, int NumberOfRecords = -1);

#ifndef USE_LASLIB
//#ifndef USE_LASDLL
	BOOL ConvertToLAS(LPCTSTR OutputFileName, BOOL CreateIndex = FALSE, BOOL PreserveLASInfo = FALSE, BOOL CleanLASPoints = FALSE, BOOL OutputLASClasses = FALSE, int MaxNumberOfLASClasses = 0, int* LASClassFlags = NULL, BOOL SingleLine = FALSE, int LineNumber = -1, BOOL TimeSlice = FALSE, double StartTime = 0.0, double StopTime = 0.0, BOOL CountSlice = FALSE, unsigned __int64 StartCount = 0, unsigned __int64 StopCount = 0);
//#endif
#endif

	long GetPosition();
	BOOL SetPosition(long Offset, int FromWhere);
	void Close();
	BOOL ReadNextRecord(LIDARRETURN* ldat, LAS_RETURNFIELDS* LASdat = NULL);
	void Rewind();
	BOOL Open(LPCTSTR szFileName);
	BOOL IsValid();
	CLidarData(LPCTSTR szFileName);
	CLidarData();
	virtual ~CLidarData();
#ifdef USE_LASLIB
	LASreadOpener lasreadopener;
	LASreader* lasreader;
#else
	laszip_POINTER lasdll_reader;
	laszip_header* lasdll_header;
	laszip_point*  lasdll_point;
	CLASFormatFile m_LASFile;
#endif

private:
	char* m_BinaryFileBuffer;
	BOOL m_HaveLocalBuffer;
	float m_Version;
	char m_LineBuffer[1024];
	int m_FileFormat;
	FILE* m_BinaryFile;
	CDataFile m_ASCIIFile;
	char buf2[16];			// dummy to prevent corruption of ASCII data file handle ?????
	BOOL m_IsCompressed;
	BOOL m_IsCOPCFormat;
	CSpatialExtent m_COPCExtent;
	int64_t m_COPCPointIndex;
	int64_t m_COPCCurrentChunk;
	BOOL m_Valid;
public:
	COPCIndex m_COPCIndex;
	BOOL IsCOPC();
	BOOL RegisterCOPCExtent(double minx, double miny, double maxx, double maxy);
	uint64_t QueryForCOPCChunks(double spacing = -1.0, BOOL verbose = FALSE);
	BOOL ReadNextCOPCRecord(LIDARRETURN* ldat, LAS_RETURNFIELDS* LASdat = NULL);
	BOOL IsCOPCExtentValid();
};

// restore structure packing
#pragma pack(pop, before_lidardata)

#endif // !defined(AFX_LIDARDATA_H__DCBA98E0_2288_11D4_A124_A80F08C10B02__INCLUDED_)
