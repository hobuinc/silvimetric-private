// DataCatalogEntry.h: interface for the CDataCatalogEntry class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DATACATALOGENTRY_H__770A58BE_9C2A_4690_BF90_B53CF28258E1__INCLUDED_)
#define AFX_DATACATALOGENTRY_H__770A58BE_9C2A_4690_BF90_B53CF28258E1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CDataCatalogEntry  
{
public:
	CDataCatalogEntry();
	virtual ~CDataCatalogEntry();

	CString m_FileName;
	long	m_CheckSum;
	double	m_MinX;
	double	m_MaxX;
	double	m_MinY;
	double	m_MaxY;
	double	m_MinZ;
	double	m_MaxZ;
	double	m_PointDensity;
	int		m_Points;

	CDataCatalogEntry& operator=(CDataCatalogEntry& src);
};

#endif // !defined(AFX_DATACATALOGENTRY_H__770A58BE_9C2A_4690_BF90_B53CF28258E1__INCLUDED_)
