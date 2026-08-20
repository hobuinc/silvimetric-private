// DataIndex.h: interface for the CDataIndex class.
//
// 3/29/2019 changes to support larger files
// in the original index, the Offset stored in the index for each point was the number of bytes from the start of point data in the point file.
// I changed this so the offset is the point index to allow larger files (more points). This means that code that reads data needs to compute the offset
// based on the point index and the size of the points
//
// There is also a variable in the header that will overflow: TotalPointsIndexed so I need to add a new variable after the old header
// I also added some information dealing with the calculation of the actual offset to points in the LAS file
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DATAINDEX_H__24EDA234_FB3E_4E62_AACF_2CF9A4D55BB2__INCLUDED_)
#define AFX_DATAINDEX_H__24EDA234_FB3E_4E62_AACF_2CF9A4D55BB2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// save current structure packing value and set to 2 so LIDARRETURN structure doesn't have any extra padding
#pragma pack(push, before_dataindex, 2)

typedef struct IndexEntry {
	unsigned char	Row;				// grid row containing point
	unsigned char	Column;				// grid column containing point
	long			Offset;				// offset to start of point data
} INDEXENTRY;

class CDataIndex  
{
public:
	BOOL UpdateChecksum(LPCTSTR DataFileName);
	void ResetRange();
	long GetCellStartValue(int Row, int Col);
	struct header {
		char		Signature[12];			// file identifier "LDAindex"
		float		Version;				// version identifier
		long		Checksum;				// checksum computed from data file to detect file changes
		int			Format;					// format of data file
		double		MinX;					// minimum x value in data
		double		MinY;					// minimum y value in data
		double		MinZ;					// minimum z value in data
		double		MaxX;					// maximum x value in data
		double		MaxY;					// maximum y value in data
		double		MaxZ;					// maximum z value in data
		int			GridCellsAcross;		// number of grid cells in x direction
		int			GridCellsUp;			// number of grid cells in y direction
		int			TotalPointsIndexed;		// total number of points in data file
	};

	struct header m_Header;

	long JumpToNextPointInRange(double minx, double miny, double maxx, double maxy, FILE* datafile = NULL);
	void Close();
	BOOL IsValid();
	BOOL Open(LPCTSTR DataFileName, BOOL UseStarts = FALSE);
	BOOL ReadHeader(FILE* handle = NULL);
	BOOL VerifyChecksum(LPCTSTR DataFileName, int UseOldMethod = FALSE);
	BOOL CheckAndCreate(LPCTSTR DataFileName, int Format, int Width = 256, int Height = 256);
	BOOL CreateIndex(LPCTSTR DataFileName, int Format, int Width = 256, int Height = 256, int NumberOfPoints = 0, double MinX = 0.0, double MaxX = 0.0, double MinY = 0.0, double MaxY = 0.0, double MinZ = 0.0, double MaxZ = 0.0, BOOL Optimize = TRUE);
	CDataIndex();
	CDataIndex(LPCTSTR DataFileName, int Format, int Width = 256, int Height = 256);
	virtual ~CDataIndex();

private:
	long* m_CellStarts;
	BOOL m_HaveStarts;
	double m_Range_MinX;
	double m_Range_MinY;
	double m_Range_MaxX;
	double m_Range_MaxY;
	unsigned char m_Range_MinCellColumn;
	unsigned char m_Range_MaxCellColumn;
	unsigned char m_Range_MinCellRow;
	unsigned char m_Range_MaxCellRow;

	BOOL m_HaveDataRange;
	BOOL WritePoint(FILE* handle = NULL);
	BOOL ReadPoint(FILE* handle = NULL);
	BOOL WriteHeader(FILE* handle = NULL);
	FILE* m_FileHandle;

	struct pt {
		unsigned char	Row;				// grid row containing point
		unsigned char	Column;				// grid column containing point
		long			Offset;				// offset to start of point data
	};
	BOOL m_Valid;

protected:
	long ComputeChecksum(LPCTSTR DataFileName, int UseOldMethod = FALSE);
	struct pt m_Point;
	CString m_DataFileName;
	CString m_IndexFileName;
};

// restore structure packing
#pragma pack(pop, before_dataindex)

#endif // !defined(AFX_DATAINDEX_H__24EDA234_FB3E_4E62_AACF_2CF9A4D55BB2__INCLUDED_)
