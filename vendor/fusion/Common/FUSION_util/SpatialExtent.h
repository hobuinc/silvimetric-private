// SpatialExtent.h: interface for the CSpatialExtent class.
//
//
// 5/6/2024 made several changes in CSpatialExtent and CSpatialExtentZ while
// adding support for COPC format point files. Main changes deal with comparing
// extent to check for overlap with an extent and one extent fully containing another.
// Also added better constructors and cleaned up some of the functions related
// to initialize and setting extent values.
// 
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SPATIALEXTENT_H__458A9201_84AE_492B_B00B_F56BA52F7918__INCLUDED_)
#define AFX_SPATIALEXTENT_H__458A9201_84AE_492B_B00B_F56BA52F7918__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CSpatialExtent  
{
public:
	BOOL IsValid();
	bool DoesRectangleOverlap(CSpatialExtent Extent);
	bool DoesRectangleOverlap(double minx, double miny, double maxx, double maxy);
	bool Contains(CSpatialExtent Extent);
	bool Contains(double minx, double miny, double maxx, double maxy);
	bool Contains(double x, double y);
	bool IsPointWithin(double x, double y);
	void Initialize();
	void CompareExtent(CSpatialExtent Extent);
	void CompareExtent(double X1, double Y1, double X2, double Y2);
	void SetAll(double X1, double Y1, double X2, double Y2);
	void ComparePt(double X, double Y);
	void Shrink(double X, double Y);
	void Expand(double X, double Y);
	double Height() {return(m_MaxY - m_MinY);}
	double Width() {return(m_MaxX - m_MinX);}
	double Area() {return(Height() * Width());}
	CSpatialExtent& operator=(CSpatialExtent& src);

	double m_MinX;
	double m_MinY;
	double m_MaxX;
	double m_MaxY;

	CSpatialExtent();
	CSpatialExtent(double X1, double Y1, double X2, double Y2);
	virtual ~CSpatialExtent();
};


class CSpatialExtentZ : public CSpatialExtent  
{
public:
	void InitializeZ();
	bool DoesRectangleOverlapZ(CSpatialExtentZ Extent);
	bool DoesRectangleOverlapZ(double minx, double miny, double minz, double maxx, double maxy, double maxz);
	bool ContainsZ(CSpatialExtentZ Extent);
	bool ContainsZ(double minx, double miny, double minz, double maxx, double maxy, double maxz);
	bool ContainsZ(double x, double y, double z);
	void CompareExtentZ(CSpatialExtentZ Extent);
	void ExpandZ(double X, double Y, double Z);
	void ShrinkZ(double X, double Y, double Z);
	void SetAllZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2);
	void ComparePtZ(double X, double Y, double Z);
	void CompareExtentZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2);
	CSpatialExtentZ();
	CSpatialExtentZ(double X1, double Y1, double Z1, double X2, double Y2, double Z2);
	virtual ~CSpatialExtentZ();
	double Depth() {return(m_MaxZ - m_MinZ);}
	CSpatialExtentZ& operator=(CSpatialExtentZ& src);

	double m_MinZ;
	double m_MaxZ;
	bool IsPointWithin(double x, double y, double z);
};

#endif // !defined(AFX_SPATIALEXTENT_H__458A9201_84AE_492B_B00B_F56BA52F7918__INCLUDED_)
