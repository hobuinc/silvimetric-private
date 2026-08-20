// TerrainSet.h: interface for the CTerrainSet class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TERRAINSET_H__D3033F35_514A_4683_80E5_5F4F1391B3B0__INCLUDED_)
#define AFX_TERRAINSET_H__D3033F35_514A_4683_80E5_5F4F1391B3B0__INCLUDED_

#include "plansdtm.h"
#include "datafile.h"

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CTerrainSet  
{
public:
	void InterpretDataAsRaster();
	BOOL SetIsRasterData();
	BOOL ModelOverlapsExtent(int Index, double MinX, double MinY, double MaxX, double MaxY);
	BOOL AddFileToSet(LPCTSTR Filename);
	double GetFirstModelColumnSpacing();
	double GetFirstModelPointSpacing();
	BOOL WasModelUsedForInternalModel(int index);
	CString InternalModelDescription();
	BOOL InternalModelIsValid();
	BOOL InternalModelResolutionWasReduced();
	BOOL AreModelsInteger();
	BOOL GetOverallExtent(double &MinX, double &MinY, double &MaxX, double &MaxY);
	BOOL AreModelElevationsInMemory(int index);
	CString GetModelFileName(int index);
	int GetModelCount();
	BOOL IsValid();
	void UnloadData();
	double InterpolateElev(double X, double Y, BOOL AccumulateValues = FALSE, BOOL InterpolateOverGaps = TRUE);
	void PreLoadData(double MinX, double MinY, double MaxX, double MaxY, BOOL CreateInternalModel = FALSE, BOOL KeepOriginalDTMCellSize = TRUE, BOOL AccumulateValues = FALSE);
	void DestroyOverview();
	BOOL CreateOverview();
	void Destroy();
	void SetFileSpec(LPCTSTR FileSpec);
	int GetError();
	CTerrainSet();
	CTerrainSet(LPCTSTR FileSpec);
	virtual ~CTerrainSet();

private:
	int m_LastModelForInterpolation;
	int m_ErrorCode;
	BOOL m_OverviewValid;
	BOOL m_SetIsRaster;
	PlansDTM m_InternalModel;
	BOOL m_HaveInternalModel;
	BOOL m_InternalModelResolutionWasReduced;
	CString m_FileSpec;
	int m_FileCount;
	PlansDTM* m_ModelHeaderList;
	BOOL* m_ModelElevLoaded;
	BOOL* m_ModelUsedForInternalModel;
	BOOL* m_ModelWithinPreloadExtent;
	BOOL* m_ModelResolutionMatchesFirst;
	CStringArray m_FileList;
	int ExpandFileSpecToList(CString FileSpec);
	int CountFilesMatchingSpec(LPCSTR FileSpec);
	BOOL RectanglesIntersect(double MinX1, double MinY1, double MaxX1, double MaxY1, double MinX2, double MinY2, double MaxX2, double MaxY2, double Expand = 0.0);
	BOOL RectanglesIntersectOrTouch(double MinX1, double MinY1, double MaxX1, double MaxY1, double MinX2, double MinY2, double MaxX2, double MaxY2, double Expand = 0.0);
	void Initialize();
protected:
	void RearrangeFileList(int Order);
private:
	// Fill in values when there is a small gap between models in the TerrainSet
	double InterpolateInGap(double X, double Y, double SearchDistance);
public:
	BOOL IsModelWithinPreloadAea(int index);
};

#endif // !defined(AFX_TERRAINSET_H__D3033F35_514A_4683_80E5_5F4F1391B3B0__INCLUDED_)
