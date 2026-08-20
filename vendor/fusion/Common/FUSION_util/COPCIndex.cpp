#include "StdAfx.h"
#include "COPCIndex.h"
#include <string.h>
#include <math.h>
#include <limits>

int compareChunk(const void* arg1, const void* arg2)
{
    if (((COPCEntry*)arg1)->offset < ((COPCEntry*)arg2)->offset)
        return -1;
    else if (((COPCEntry*)arg1)->offset > ((COPCEntry*)arg2)->offset)
        return 1;
    else
        return 0;
}

// constructor
COPCIndex::COPCIndex()
{
    m_Valid = false;
    m_Entries = 0;
    m_ChunkTable = NULL;
}

COPCIndex::COPCIndex(LPCSTR DataFileName)
{
    m_Valid = false;
    m_Entries = 0;
    m_ChunkTable = NULL;

    ReadIndex(DataFileName);
}

// destructor
COPCIndex::~COPCIndex()
{
    Destroy();
}

// Verify that LAZ file is COPC and read index
bool COPCIndex::ReadIndex(LPCSTR DataFileName)
{
    // open file for direct reading
    FILE* f = fopen(DataFileName, "rb");
    if (f != NULL) {
        if (ReadInfo(f)) {
            // read all pages starting with root page...counting pages only
            ReadPage(f, m_COPCInfo.root_hier_offset, (int32_t)m_COPCInfo.root_hier_size / 32, true);

            // allocate space for chunks
            m_ChunkTable = new COPCEntry[(unsigned int) m_Entries];
            
            // reset the entry counter...use to add chunks to chunk table
            m_Entries = 0;

            // read all pages starting with root page
            ReadPage(f, m_COPCInfo.root_hier_offset, (int32_t) m_COPCInfo.root_hier_size / 32);

            m_Valid = true;

            SortChunkTable();
            AddCumulativeCounts();
        }
        fclose(f);
    }
    else {
        printf("Could not open %s\n", DataFileName);
    }

    return m_Valid;
}


void COPCIndex::PrintInfo()
{
    if (m_Valid) {
        printf("COPC Info:\n");
        printf("  Center X:%.2lf  Y:%.2lf  Z:%.2lf\n", m_COPCInfo.center_x, m_COPCInfo.center_y, m_COPCInfo.center_z);
        printf("  Half size:%.2lf\n", m_COPCInfo.halfsize);
        printf("  GPS time min:%.6lf  max:%.6lf\n", m_COPCInfo.gpstime_minimum, m_COPCInfo.gpstime_maximum);
        printf("  Root hier offset:%I64u  size:%I64u\n", m_COPCInfo.root_hier_offset, m_COPCInfo.root_hier_size);
        printf("  Spacing:%.2lf\n", m_COPCInfo.spacing);
        printf("  Entries:%I64u\n", m_Entries);
    }
}


bool COPCIndex::ReadInfo(FILE* f)
{
    char buf[590];

    // read 589 bytes
    if (fread(buf, 589, 1, f) == 1) {
        // validate LAS
        char signature[5];
        strncpy(signature, buf, 4);
        signature[4] = 0;
        if (strcmp(signature, "LASF") == 0) {
            // validate copc
            strncpy(signature, &buf[377], 4);
            signature[4] = 0;
            if (strcmp(signature, "copc") == 0) {
                // validate copc version
                if (buf[393] == 1 && buf[394] == 0) {
                    // get point data record format, offset 104
                    unsigned char prf = buf[104] & 63;

                    // get point record length, 2-byte unsigned int at offset 105, little endian order
                    uint16_t prl = (buf[106] << 8) + buf[105];

                    // read copc info VLR, offset 429
                    char* c = &buf[429];
                    m_COPCInfo.center_x = *(double*)c;
                    c += sizeof(m_COPCInfo.center_x);
                    m_COPCInfo.center_y = *(double*)c;
                    c += sizeof(m_COPCInfo.center_y);
                    m_COPCInfo.center_z = *(double*)c;
                    c += sizeof(m_COPCInfo.center_z);
                    m_COPCInfo.halfsize = *(double*)c;
                    c += sizeof(m_COPCInfo.halfsize);
                    m_COPCInfo.spacing = *(double*)c;
                    c += sizeof(m_COPCInfo.spacing);
                    m_COPCInfo.root_hier_offset = *(uint64_t*)c;
                    c += sizeof(m_COPCInfo.root_hier_offset);
                    m_COPCInfo.root_hier_size = *(uint64_t*)c;
                    c += sizeof(m_COPCInfo.root_hier_size);
                    m_COPCInfo.gpstime_minimum = *(double*)c;
                    c += sizeof(m_COPCInfo.gpstime_minimum);
                    m_COPCInfo.gpstime_maximum = *(double*)c;
                    c += sizeof(m_COPCInfo.gpstime_maximum);
                    for (int i = 0; i < 11; i++) {
                        m_COPCInfo.reserved[i] = *(uint64_t*)c;
                        c += sizeof(m_COPCInfo.reserved[i]);
                    }

                    // Compute bounds
                    m_RootBounds.SetAllZ(
                        m_COPCInfo.center_x - m_COPCInfo.halfsize,
                        m_COPCInfo.center_y - m_COPCInfo.halfsize,
                        m_COPCInfo.center_z - m_COPCInfo.halfsize,
                        m_COPCInfo.center_x + m_COPCInfo.halfsize,
                        m_COPCInfo.center_y + m_COPCInfo.halfsize,
                        m_COPCInfo.center_z + m_COPCInfo.halfsize
                    );

                    return(true);
                }
            }
        }
    }

    // something was wrong
    return false;
}


bool COPCIndex::ReadEntry(FILE* f, COPCEntry* entry)
{
    size_t cnt = 0;

    // read key
    cnt += fread(&(entry->key.level), sizeof(int32_t), 1, f);
    cnt += fread(&(entry->key.x), sizeof(int32_t), 1, f);
    cnt += fread(&(entry->key.y), sizeof(int32_t), 1, f);
    cnt += fread(&(entry->key.z), sizeof(int32_t), 1, f);

    // read
    cnt += fread(&(entry->offset), sizeof(int64_t), 1, f);
    cnt += fread(&(entry->byteSize), sizeof(int32_t), 1, f);
    cnt += fread(&(entry->pointCount), sizeof(int32_t), 1, f);

    if (cnt == 7)
        return true;

    return false;
}

bool COPCIndex::ReadPage(FILE* f, uint64_t offset, int32_t entries, bool countOnly)
{
    COPCEntry e;

    // jump to start of page
    _fseeki64(f, offset, SEEK_SET);

    for (int32_t i = 0; i < entries; i++) {
        // jump to start of entry
        _fseeki64(f, offset + (uint64_t) i * 32, SEEK_SET);

        ReadEntry(f, &e);

        if (!countOnly) {
            ComputeEntryBounds(&e, &m_RootBounds, &e.m_Bounds);

            // compute point spacing
            e.m_Spacing = m_COPCInfo.spacing / pow(2, e.key.level);

            // clear flag
            e.m_Active = NOOVERLAP;

            // save to chunk table
            m_ChunkTable[m_Entries].byteSize = e.byteSize;
            m_ChunkTable[m_Entries].key = e.key;
            m_ChunkTable[m_Entries].m_Bounds = e.m_Bounds;
            m_ChunkTable[m_Entries].m_Spacing = e.m_Spacing;
            m_ChunkTable[m_Entries].offset = e.offset;
            m_ChunkTable[m_Entries].pointCount = e.pointCount;
            m_ChunkTable[m_Entries].m_StartingPointIndex = 0;
            m_ChunkTable[m_Entries].m_Active = e.m_Active;
        }

        m_Entries++;

        // recurse into hierarchy
        if (e.pointCount == -1)
            ReadPage(f, e.offset, e.byteSize / 32, countOnly);
    }

    return false;
}

void COPCIndex::ComputeEntryBounds(COPCEntry* e, CSpatialExtentZ* root, CSpatialExtentZ* ext)
{
    // compute extent code modified from
    // https://github.com/PDAL/PDAL/blob/master/io/private/copc/Key.hpp#L113

    double cellWidth = (root->m_MaxX - root->m_MinX) / pow(2, e->key.level);

    // The test in each of these is to avoid unnecessary rounding errors when
    // we know the actual value.
    ext->m_MinX = (e->key.x == 0 ? root->m_MinX : root->m_MinX + (cellWidth * e->key.x));
    ext->m_MaxX = (e->key.x == e->key.level ? root->m_MaxX : root->m_MinX + (cellWidth * (e->key.x + 1)));
    ext->m_MinY = (e->key.y == 0 ? root->m_MinY : root->m_MinY + (cellWidth * e->key.y));
    ext->m_MaxY = (e->key.y == e->key.level ? root->m_MaxY : root->m_MinY + (cellWidth * (e->key.y + 1)));
    ext->m_MinZ = (e->key.z == 0 ? root->m_MinZ : root->m_MinZ + (cellWidth * e->key.z));
    ext->m_MaxZ = (e->key.z == e->key.level ? root->m_MaxZ : root->m_MinZ + (cellWidth * (e->key.z + 1)));
}

void COPCIndex::PrintChunkTable(bool ActiveOnly)
{
    if (m_Valid) {
        for (uint64_t i = 0; i < m_Entries; i++) {
            if (ActiveOnly) {
                if (m_ChunkTable[i].m_Active) {
                    // print entry info
                    PrintChunkInfo(i);
                }
            }
            else {
                // print entry info
                PrintChunkInfo(i);
            }
        }
    }
}


uint64_t COPCIndex::QueryChunks(double X1, double Y1, double X2, double Y2, double Spacing)
{
    CSpatialExtent ext;
    ext.SetAll(X1, Y1, X2, Y2);

    uint64_t cnt = 0;
    if (m_Valid) {
        for (uint64_t i = 0; i < m_Entries; i++) {
            m_ChunkTable[i].m_Active = NOOVERLAP;
            
            // only consider chunks that have points
            if (m_ChunkTable[i].pointCount <= 0)
                continue;

            if (ext.Contains(m_ChunkTable[i].m_Bounds)) {
                if (Spacing > 0.0) {
                    if (m_ChunkTable[i].m_Spacing <= Spacing)
                        continue;
                }
                m_ChunkTable[i].m_Active = FULLOVERLAP;
            }
            else if (m_ChunkTable[i].m_Bounds.DoesRectangleOverlap(ext)) {
                if (Spacing > 0.0) {
                    if (m_ChunkTable[i].m_Spacing <= Spacing)
                        continue;
                }
                m_ChunkTable[i].m_Active = PARTIALOVERLAP;
            }

            if (m_ChunkTable[i].m_Active)
                cnt++;
        }
    }
    return cnt;
}


uint64_t COPCIndex::QueryChunksZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2, double Spacing)
{
    CSpatialExtentZ ext;
    ext.SetAllZ(X1, Y1, Z1, X2, Y2, Z2);

    // don't want to use the 2D version because it changes the active status
    uint64_t cnt = 0;
    if (m_Valid) {
        for (uint64_t i = 0; i < m_Entries; i++) {
            m_ChunkTable[i].m_Active = NOOVERLAP;

            // only consider chunks that have points
            if (m_ChunkTable[i].pointCount <= 0)
                continue;

            if (ext.ContainsZ(m_ChunkTable[i].m_Bounds)) {
                if (Spacing > 0.0) {
                    if (m_ChunkTable[i].m_Spacing <= Spacing)
                        continue;
                }

                m_ChunkTable[i].m_Active = FULLOVERLAP;
            }
            else if (m_ChunkTable[i].m_Bounds.DoesRectangleOverlapZ(ext)) {
                if (Spacing > 0.0) {
                    if (m_ChunkTable[i].m_Spacing <= Spacing)
                        continue;
                }

                m_ChunkTable[i].m_Active = PARTIALOVERLAP;
            }
            if (m_ChunkTable[i].m_Active)
                cnt++;
        }
    }
    return cnt;
}


void COPCIndex::PrintChunkInfo(uint64_t chunk)
{
    char olap[3][8] = {"NONE", "PARTIAL", "FULL"};

    printf("level:%i  overlap: %s  x:%i  y:%i  z:%i  spacing: %.2lf  offset:%I64u  bytes:%i  pts:%i\n    cpts:%I64u LL:(%.2lf,%.2lf,%.2lf)  UR:(%.2lf,%.2lf,%.2lf)\n",
        m_ChunkTable[chunk].key.level, olap[m_ChunkTable[chunk].m_Active], m_ChunkTable[chunk].key.x, m_ChunkTable[chunk].key.y, m_ChunkTable[chunk].key.z,
        m_ChunkTable[chunk].m_Spacing,
        m_ChunkTable[chunk].offset, m_ChunkTable[chunk].byteSize, m_ChunkTable[chunk].pointCount, m_ChunkTable[chunk].m_StartingPointIndex,
        m_ChunkTable[chunk].m_Bounds.m_MinX, m_ChunkTable[chunk].m_Bounds.m_MinY, m_ChunkTable[chunk].m_Bounds.m_MinZ,
        m_ChunkTable[chunk].m_Bounds.m_MaxX, m_ChunkTable[chunk].m_Bounds.m_MaxY, m_ChunkTable[chunk].m_Bounds.m_MaxZ);
}


bool COPCIndex::SortChunkTable()
{
    if (m_Valid) {
        qsort(m_ChunkTable, (size_t) m_Entries, sizeof(COPCEntry), compareChunk);

        return true;
    }
    return false;
}


void COPCIndex::AddCumulativeCounts()
{
    if (m_Valid && m_Entries) {
        m_ChunkTable[0].m_StartingPointIndex = 0;
        for (uint64_t i = 1; i < m_Entries; i++) {
            m_ChunkTable[i].m_StartingPointIndex = m_ChunkTable[i - 1].m_StartingPointIndex + (uint64_t) m_ChunkTable[i - 1].pointCount;
        }
    }
}

uint64_t COPCIndex::NumberOfActiveChunks()
{
    uint64_t cnt = 0;

    for (uint64_t i = 1; i < m_Entries; i++) {
        if (m_ChunkTable[i].m_Active > 0)
            cnt++;
    }
    return(cnt);
}


bool COPCIndex::IsValid()
{
    // TODO: Add your implementation code here.
    return m_Valid;
}


uint64_t COPCIndex::ChunkCount()
{
    if (m_Valid)
        return(m_Entries);

    return(0);
}


int COPCIndex::ChunkStatus(uint64_t chunk)
{
    if (m_Valid && m_Entries > 0 && chunk >= 0 && chunk < m_Entries)
        return(m_ChunkTable[chunk].m_Active);

    return(NOOVERLAP);
}


uint64_t COPCIndex::FirstPointIndex(uint64_t chunk)
{
    if (m_Valid && m_Entries > 0 && chunk >= 0 && chunk < m_Entries)
        return(m_ChunkTable[chunk].m_StartingPointIndex);

    return(ULLONG_MAX);
}


uint64_t COPCIndex::ChunkPointCount(uint64_t chunk)
{
    if (m_Valid && m_Entries > 0 && chunk >= 0 && chunk < m_Entries)
        return(m_ChunkTable[chunk].pointCount);

    return(0);
}

// Notes:
// to make this work like my existing index:
// when you reset the range, do a new spatial query for chunks covering the extent
// keep track of the current chunk and, maybe, point in the chunk
// read and test points just like JumpToNextPointInRange() in CDataIndex
// when you reach the end of the points in a chunk, go to the next active chunk
// if no more active chunks, you are done with points in the range

bool COPCIndex::IsCOPCFile(LPCSTR DataFileName)
{
    bool ret = false;

    // open file for direct reading
    FILE* f = fopen(DataFileName, "rb");
    if (f != NULL) {
        ret = ReadInfo(f);
        fclose(f);
    }
    return(ret);
}


void COPCIndex::Destroy()
{
    if (m_ChunkTable)
        delete[] m_ChunkTable;

    m_ChunkTable = NULL;

    m_Valid = false;
    m_Entries = 0;
}


double COPCIndex::GetMaximumSpacing()
{
    double maxSpacing = 0.0;
    if (m_Valid) {
        for (uint64_t i = 1; i < m_Entries; i++) {
            maxSpacing = max(maxSpacing, m_ChunkTable[i].m_Spacing);
        }
    }
    return(maxSpacing);
}
