// COPCIndex
//
// Read the index information from COPC formatted files and rearrange the chunk table
// so we can use the lasZIP dll to read chunks.
//
// 4/23/2024	Original coding
// I worked on this prior to this date but it took some time for me to figure out how to read the 
// data using Martin's lasZIP dll without changing over to lazperf. The trick was in the ability to
// seek to a specific point in LAZ files and sorting the chunk table so the entries are in sequential 
// order. This allows computation of the cumulative number of points in the chunks so you can easily seek
// to the first point in each chunk. From there, Martin's code works just fine and I don't think there
// is any penalty associated with reading the chunks.
//
// With my index files, there is no relationship between the LAZ chunks and the spatial index. This means
// that points within a spatial extent may fall in many LAZ chunks and the points may not be ordered so the
// points in the same chunk are sequential. This results in a 3x performance penalty when using my indexes
// with LAZ files. With COPC, all points in a chunk fall within a known spatial extent so once we know that the
// chunk's extent overlaps our AOI, we stay in the same chunk for all points. This should make COPC reading more
// efficient than reading through my indexes. Initial testing in FUSION.exe shows good performance with COPC
// format files whereas performance with LAZ files and my indexes is horrible (non-usable). I have comments in the
// GridMetrics code that performance with LAZ and my index files is 3x slower than without the index (still with LAZ
// file). I suspect this is because the code is jumping around in the LAZ point blocks instead of reading all points
// in a block and then moving to the next block as will be the case with COPC.
//
#pragma once
#include <cstdint>
#include <stdio.h>
#include "SpatialExtent.h"

//#define BOOL bool
#define LPCSTR const char*

struct COPCInfo
{
	// Actual (unscaled) X coordinate of center of octree
	double center_x;

	// Actual (unscaled) Y coordinate of center of octree
	double center_y;

	// Actual (unscaled) Z coordinate of center of octree
	double center_z;

	// Perpendicular distance from the center to any side of the root node.
	double halfsize;

	// Space between points at the root node.
	// This value is halved at each octree level
	double spacing;

	// File offset to the first hierarchy page
	uint64_t root_hier_offset;

	// Size of the first hierarchy page in bytes
	uint64_t root_hier_size;

	// Minimum of GPSTime
	double gpstime_minimum;

	// Maximum of GPSTime
	double gpstime_maximum;

	// Must be 0
	uint64_t reserved[11];
};

struct COPCVoxelKey
{
	// A value < 0 indicates an invalid VoxelKey
	int32_t level;
	int32_t x;
	int32_t y;
	int32_t z;
};

#define NOOVERLAP	0
#define PARTIALOVERLAP	1
#define FULLOVERLAP		2

struct COPCEntry
{
	// EPT key of the data to which this entry corresponds
	COPCVoxelKey key;

	// Absolute offset to the data chunk if the pointCount > 0.
	// Absolute offset to a child hierarchy page if the pointCount is -1.
	// 0 if the pointCount is 0.
	uint64_t offset;

	// Size of the data chunk in bytes (compressed size) if the pointCount > 0.
	// Size of the hierarchy page if the pointCount is -1.
	// 0 if the pointCount is 0.
	int32_t byteSize;

	// If > 0, represents the number of points in the data chunk.
	// If -1, indicates the information for this octree node is found in another hierarchy page.
	// If 0, no point data exists for this key, though may exist for child entries.
	int32_t pointCount;

	// spatial bounds...computed
	CSpatialExtentZ m_Bounds;

	// point spacing...computed
	double m_Spacing;

	// flag indicating chunk is active in the latest spatial query
	int m_Active;

	// cumulative point count...populated after sorting by offset
	uint64_t m_StartingPointIndex;
};

//struct COPCPage
//{
//	COPCEntry entries[page_size / 32];
//};

class COPCIndex
{
public:
	COPCIndex();
	COPCIndex(LPCSTR DataFileName);
	virtual ~COPCIndex();

	bool IsValid();
	uint64_t ChunkCount();

	bool ReadIndex(LPCSTR DataFileName);

	uint64_t NumberOfActiveChunks();
	int ChunkStatus(uint64_t chunk);

	void PrintInfo();
	void PrintChunkTable(bool ActiveOnly = false);
	void PrintChunkInfo(uint64_t chunk);

	uint64_t QueryChunks(double X1, double Y1, double X2, double Y2, double Spacing = -1.0);
	uint64_t QueryChunksZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2, double Spacing = -1.0);

	CSpatialExtentZ m_RootBounds;

private:
	bool m_Valid;

	bool ReadEntry(FILE* f, COPCEntry* e);
	bool ReadInfo(FILE* f);
	bool ReadPage(FILE* f, uint64_t offset, int32_t entries, bool countOnly = false);
	void ComputeEntryBounds(COPCEntry* e, CSpatialExtentZ* root, CSpatialExtentZ* ext);
	bool SortChunkTable();
	void AddCumulativeCounts();

	COPCInfo m_COPCInfo;
	uint64_t m_Entries;
	COPCEntry* m_ChunkTable;
public:
	uint64_t FirstPointIndex(uint64_t chunk);
	uint64_t ChunkPointCount(uint64_t chunk);
	bool IsCOPCFile(LPCSTR DataFileName);
	void Destroy();
	double GetMaximumSpacing();
};

