// SpatialExtent.cpp: implementation of the CSpatialExtent class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SpatialExtent.h"
#include <math.h>
#include <float.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSpatialExtent::CSpatialExtent()
{
	Initialize();
}

CSpatialExtent::CSpatialExtent(double X1, double Y1, double X2, double Y2)
{
	Initialize();

	SetAll(X1, Y1, X2, Y2);
}

CSpatialExtent::~CSpatialExtent()
{
}

void CSpatialExtent::Initialize()
{
	// these are initialized to these values instead of DBL_MIN and DBL_MAX. the constants caused trouble in dialogs due to DDX_Text()
	m_MinX = 1000000000.0;
	m_MinY = 1000000000.0;
	m_MaxX = -1000000000.0;
	m_MaxY = -1000000000.0;
}

CSpatialExtent& CSpatialExtent::operator=(CSpatialExtent& src)
{
	m_MinX = src.m_MinX;
	m_MinY = src.m_MinY;
	m_MaxX = src.m_MaxX;
	m_MaxY = src.m_MaxY;

	return *this;
}

void CSpatialExtent::Expand(double X, double Y)
{
	m_MinX -= X;
	m_MaxX += X;
	m_MinY -= Y;
	m_MaxY += Y;
}

void CSpatialExtent::Shrink(double X, double Y)
{
	m_MinX += X;
	m_MaxX -= X;
	m_MinY += Y;
	m_MaxY -= Y;
}

void CSpatialExtent::ComparePt(double X, double Y)
{
	m_MinX = min(m_MinX, X);
	m_MinY = min(m_MinY, Y);
	m_MaxX = max(m_MaxX, X);
	m_MaxY = max(m_MaxY, Y);
}

void CSpatialExtent::SetAll(double X1, double Y1, double X2, double Y2)
{
	m_MinX = X1;
	m_MinY = Y1;
	m_MaxX = X2;
	m_MaxY = Y2;
}

void CSpatialExtent::CompareExtent(double X1, double Y1, double X2, double Y2)
{
	m_MinX = min(m_MinX, X1);
	m_MinY = min(m_MinY, Y1);
	m_MaxX = max(m_MaxX, X2);
	m_MaxY = max(m_MaxY, Y2);
}

void CSpatialExtent::CompareExtent(CSpatialExtent Extent)
{
	CompareExtent(Extent.m_MinX, Extent.m_MinY, Extent.m_MaxX, Extent.m_MaxY);
}

//////////////////////////////////////////////////////////////////////
// CSpatialExtentZ Class
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSpatialExtentZ::CSpatialExtentZ() : CSpatialExtent()
{
	InitializeZ();
}

CSpatialExtentZ::CSpatialExtentZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2)
{
	InitializeZ();

	SetAllZ(X1, Y1, Z1, X2, Y2, Z2);
}

CSpatialExtentZ::~CSpatialExtentZ()
{
}

void CSpatialExtentZ::InitializeZ()
{
	// initialize XY values
	Initialize();

	// these are initialized to these values instead of DBL_MIN and DBL_MAX. the constants caused trouble in dialogs due to DDX_Text()
	m_MinZ = 1000000000.0;
	m_MaxZ = -1000000000.0;
}

void CSpatialExtentZ::CompareExtentZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2)
{
	CSpatialExtent::CompareExtent(X1, Y1, X2, Y2);

	m_MinZ = min(m_MinZ, Z1);
	m_MaxZ = max(m_MaxZ, Z2);
}

void CSpatialExtentZ::ComparePtZ(double X, double Y, double Z)
{
	CSpatialExtent::ComparePt(X, Y);

	m_MinZ = min(m_MinZ, Z);
	m_MaxZ = max(m_MaxZ, Z);
}

void CSpatialExtentZ::SetAllZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2)
{
	CSpatialExtent::SetAll(X1, Y1, X2, Y2);

	m_MinZ = Z1;
	m_MaxZ = Z2;
}

void CSpatialExtentZ::ShrinkZ(double X, double Y, double Z)
{
	CSpatialExtent::Shrink(X, Y);

	m_MinZ += Z;
	m_MaxZ -= Z;
}

void CSpatialExtentZ::ExpandZ(double X, double Y, double Z)
{
	CSpatialExtent::Expand(X, Y);

	m_MinZ -= Z;
	m_MaxZ += Z;
}

CSpatialExtentZ& CSpatialExtentZ::operator=(CSpatialExtentZ& src)
{
	CSpatialExtent::operator=(src);
//	m_MinX = src.m_MinX;
//	m_MinY = src.m_MinY;
	m_MinZ = src.m_MinZ;
//	m_MaxX = src.m_MaxX;
//	m_MaxY = src.m_MaxY;
	m_MaxZ = src.m_MaxZ;

	return *this;
}

void CSpatialExtentZ::CompareExtentZ(CSpatialExtentZ Extent)
{
	CompareExtentZ(Extent.m_MinX, Extent.m_MinY, Extent.m_MinZ, Extent.m_MaxX, Extent.m_MaxY, Extent.m_MaxZ);
}


bool CSpatialExtent::IsPointWithin(double x, double y)
{
	// NOTE that the test omits points on the top and right side of the extent...this is so you can have adjacent AOIs without duplicating points
	if (x >= m_MinX && x < m_MaxX) {
		if (y >= m_MinY && y < m_MaxY) {
			return(true);
		}
	}
	return(false);
}

bool CSpatialExtent::DoesRectangleOverlap(double minx, double miny, double maxx, double maxy)
{
	// test to see if the specified rectangle overlaps the extent
	if (minx > m_MaxX)
		return(false);
	if (maxx < m_MinX)
		return(false);
	if (miny > m_MaxY)
		return(false);
	if (maxy < m_MinY)
		return(false);

	return(true);
}

bool CSpatialExtent::DoesRectangleOverlap(CSpatialExtent Extent)
{
	return(DoesRectangleOverlap(Extent.m_MinX, Extent.m_MinY, Extent.m_MaxX, Extent.m_MaxY));
}

BOOL CSpatialExtent::IsValid()
{
	if (m_MaxX > m_MinX && m_MaxY > m_MinY)
		return(TRUE);

	return(FALSE);
}


bool CSpatialExtentZ::IsPointWithin(double x, double y, double z)
{
	if (CSpatialExtent::IsPointWithin(x, y)) {
		if (z >= m_MinZ && z < m_MaxZ)
			return(true);
	}
		
	return false;
}

bool CSpatialExtent::Contains(double x, double y)
{
	return(IsPointWithin(x, y));
}

bool CSpatialExtent::Contains(double minx, double miny, double maxx, double maxy)
{
	// test to see if the extent fully contains the specified rectangle
	if (m_MinX > minx || m_MaxX < maxx)
		return(false);
	if (m_MaxY < maxy || m_MinY > miny)
		return(false);
	return (true);
}

bool CSpatialExtent::Contains(CSpatialExtent Extent)
{
	return(Contains(Extent.m_MinX, Extent.m_MinY, Extent.m_MaxX, Extent.m_MaxY));
}

bool CSpatialExtentZ::DoesRectangleOverlapZ(double minx, double miny, double minz, double maxx, double maxy, double maxz)
{
	// test to see if the specified rectangle overlaps the extent
	if (minx > m_MaxX)
		return(false);
	if (maxx < m_MinX)
		return(false);
	if (miny > m_MaxY)
		return(false);
	if (maxy < m_MinY)
		return(false);
	if (minz > m_MaxZ)
		return(false);
	if (maxz < m_MinZ)
		return(false);

	return(true);
}

bool CSpatialExtentZ::DoesRectangleOverlapZ(CSpatialExtentZ Extent)
{
	return(DoesRectangleOverlapZ(Extent.m_MinX, Extent.m_MinY, Extent.m_MinZ, Extent.m_MaxX, Extent.m_MaxY, Extent.m_MaxZ));
}

bool CSpatialExtentZ::ContainsZ(double x, double y, double z)
{
	return(IsPointWithin(x, y, z));
}

bool CSpatialExtentZ::ContainsZ(double minx, double miny, double minz, double maxx, double maxy, double maxz)
{
	// test to see if the specified rectangle fully contains the extent
	// test to see if the extent fully contains the specified rectangle
	if (m_MinX > minx || m_MaxX < maxx)
		return(false);
	if (m_MaxY < maxy || m_MinY > miny)
		return(false);
	if (m_MaxZ < maxz || m_MinZ > minz)
		return(false);
	return (true);
}

bool CSpatialExtentZ::ContainsZ(CSpatialExtentZ Extent)
{
	return(ContainsZ(Extent.m_MinX, Extent.m_MinY, Extent.m_MinZ, Extent.m_MaxX, Extent.m_MaxY, Extent.m_MaxZ));
}

