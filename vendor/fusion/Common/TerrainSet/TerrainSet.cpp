// TerrainSet.cpp: implementation of the CTerrainSet class.
//
//////////////////////////////////////////////////////////////////////
//
// 4/15/2010
//	Improved logic so that we can build an internal model using several individual DTM files and use the model to interpolate values
//	instead of loading several models into memory. This cuts down the amount of memory needed when working with small areas (compared
//	to the size of individual DTM files). This also lets us compute the amount of memory needed to provide models for a specific
//	extent to help when processing large data blocks.
//
// 8/18/2010
//	Further improved logic when creating internal models to only create a new model in memory when the requested area is more than 25%
//	smaller that the area loaded in memory or if a model's elevations could not be loaded into memory
//
// 11/5/2010
//	Added GetFirstModelColumnSpacing() and GetFirstModelPointSpacing() functions to return cell size information for first model. Changed
//	several variables to remove references to "ground" since this class is used for generic DTM handling. Improved the InterpolateElev()
//	function to remember the last model used for a point and try it first for a new location. This dramatically improves performance when
//	interpolating a new grid or other "ordered" set of points. May slow things down when working with random point locations.
//
// 9/1/2011
//	Modified the logic used to create an in-memory ground model so it doesn't try to populate the internal model using separate models
//	unless they overlap the area of interest. Previous logic was flawed and tested every model for every point in the internal model.
//
// 3/5/2013
//	Added ability to manually add individual files to the set. This involved pretty minor changes but was needed for area processing
//	tools and to allow models to come from several folders. The alternative would have been to create a list file and then call
//	SetFileSpec() with the name of the list file.
//
// 9/30/2014
//	Implemented sorting of the file list to force the use of the highest resolution DTMs first. This allows the use of a "background" DTM to fill in areas
//	within or adjacent to an acquisition using a lower-resolution DTM
//
// 1/27/2015
//	Looking for ways to speed up the TPI calculation but not having much luck.
//
//	Added a check when building an internal model so an attempt is made to interpolate an elevation from a particular model only if the
//	point is within the model extent. This might speed up the creation of the internal model a small amount.
//
// 2/24/2015
//	Added the option to automatically increase the cell size when trying to create an internal DTM. Logic will multiply the starting cell size
//	by 1.4142 to halve the resolution. This is repeated until an internal model can be created.
//
// 2/26/2015
//	Cleaned up a memeory leak associated with RearrangeFileList(). Temporary memory was being allocated twice but only freed once.
//
// 3/26/2015
//	Added options to use the DTM files as raster data. Yet another case of using the same format for surface and rasters causing problems.
//	When working with raster data, we don't want any interpolation to happen as this changes values within the dataset and really causes
//	problems when you have NODATA cells since the interploation logic won't return a value if any of the 4 surrounding cell corners is NODATA.
//
//	The changes involve added a flag (m_SetIsRaster) to indicate that the data should be interpretted as raster data and changing the behavior 
//	of InterpolateElev() to use the flag to trigger different logic that does not interpolate values from the grids. I also made changes in the 
//	PlansDTM class to allow retrieval of the largest value for the "cell" surrounding a given XY location.
//
//	I ran into trouble using this class to hold point densities and then trying to get the number of points within a rectangular extent. The NODATA
//	issue forces all of the cells on edges of the data coverage to appear to have no points when in reality they might have lots of points. This was
//	resulting in an underestimate of the number of points in the extent and eventually failure when computing metrics for a tile whose size was based
//	on the point estimate.
//
// 2/13/2018
//	Added code to trim leading and trailing spaces from file names read from a list file. If names in the list file had extra spaces (usually trailing),
//	things could break. I didn't have any cases where this was the case but did have problems reading lists of input points files in other tools.
//
//	3/16/2020
//	Added the InterpolateInGap() function to deal with gaps between models.This is to specifically address problems with 3DEP DEMs where
//	the coverage is based on a raster interpretation rather than a lattice interpretation.The logic uses the cell size of the highest - resolution model
//	as the search distance.This should be OK for the 3DEP DEMs but this could be changed to "fill" larger gaps.For 3DEP DEMs, the gap is 1
//	cell so the search distance could be dropped to the(cell size / 2).However, this would be interpolating values on the edge of the DTM and there
//	could be some odd behavior at the edges(potential edge mismatch).
//
//	Also changed the logic related to creating in-memory DTMs to fill in gaps in the surface after loading all the data. This required new logic as the 
//	FillHoles() function in the underlying DTM class would not work for the types of gaps we are seeing with USGS DEMS. With these models, the gaps
//	span the full width and height of the model (made up of multiple merged DTMS or gaps between separate models when kept on disk) and the logic
//	in FillHoles() is designed so it does not propogate values from te interior of a model to the edges when there are no valid values within the 
//	areas being filled.
//
//	*****This change eliminates the need to use the RepairGridDTM tool when using data from the USGS 3DEP program prior to use DTMs in AreaProcessor.
//	It may also eliminate the need for RepairGridDTM when using data from other sources.In general, the RepairGridDTM tool was needed whenever DEM
//	data were delivered in ESRI GRID format that did not include overlap between adjacent tiles.In reality, no overlap between raster tiles means
//	there is a gap in coverage between the tiles unless tiles are mosaiced prior to use.
//
// 9/22/2020 
//	Made some additional changes to help with interpolation when using several surface files and added the m_ModelUsedForInternalModel flags to
//	shorten the list when models are preloaded to cover a specific extent. Prior logic worked but when it encountered a situation where there was
//	a gap in coverage or a shift to a low resolution ground model (compared to the highest in the set of models in use), it would start evaluating
//	all models in the list provided when the TerrainSet was created. This list could include many more models that didn't cover the extent needed
//	so there was a bunch of time being wasted unnecessarily checking models leading to a 10X run time or longer in some cases.
//
// 3/2/2022
//	Still more changes to deal with gaps between ground models. There were problems with the interpolation logic when a coarse-resolution surface
//	was included in the set of ground models. Also identified a special case where a tile edge was falling in the gap so the interpolation logic
//	deisgned to fill small gaps between models didn't have data on one side (tile extent did not overlap surface extent of adjacent surface file.
//	New (additional) logic has special interpolation case for the edges of the in-memory model. For cases where models are not used to create an
//	in-memory model, I added a small buffer (4 X fine model resolution) to the extent when testing for model coverage so adjacent models are used
//	as part of the set providing coverage. This will allow the use of adjacent model to fill voids around the edges even when the needed extent
//	does not overlap the model.
//
//////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include <math.h>
#include <io.h>
#include "TerrainSet.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

typedef struct {
	int Index;
	double CellArea;
	CString FileName;
} TILEINFO;

// function to compare DTM headers based the cell size
int CompareDTMs(const void *a1, const void *a2)
{
	if (((TILEINFO*) a1)->CellArea < ((TILEINFO*) a2)->CellArea)
		return(-1);
	else if (((TILEINFO*) a1)->CellArea > ((TILEINFO*) a2)->CellArea)
		return(1);
	else
		return(0);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTerrainSet::CTerrainSet()
{
	Initialize();
}

CTerrainSet::CTerrainSet(LPCTSTR FileSpec)
{
	Initialize();

	SetFileSpec(FileSpec);
	m_LastModelForInterpolation = -1;
}

CTerrainSet::~CTerrainSet()
{
	Destroy();
	m_LastModelForInterpolation = -1;
}

int CTerrainSet::GetError()
{
	return(m_ErrorCode);
}

void CTerrainSet::Initialize()
{
	m_FileCount = 0;
	m_FileSpec.Empty();
	m_ModelHeaderList = NULL;
	m_ModelElevLoaded = NULL;
	m_ModelUsedForInternalModel = NULL;
	m_ModelWithinPreloadExtent = NULL;
	m_ModelResolutionMatchesFirst = NULL;


	m_OverviewValid = FALSE;
	m_HaveInternalModel = FALSE;
	m_InternalModelResolutionWasReduced = FALSE;

	m_SetIsRaster = FALSE;
}

void CTerrainSet::SetFileSpec(LPCTSTR FileSpec)
{
	Initialize();
	m_FileSpec = FileSpec;
	CreateOverview();
}

int CTerrainSet::CountFilesMatchingSpec(LPCSTR FileSpec)
{
	if (FileSpec[0] == '\0')
		return(0);

	CFileFind finder;
	int FileCount = 0;
	BOOL bWorking = finder.FindFile(FileSpec);
	while (bWorking) {
		bWorking = finder.FindNextFile();
		FileCount ++;
	}

	return(FileCount);
}

int CTerrainSet::ExpandFileSpecToList(CString FileSpec)
{
	int Count = CountFilesMatchingSpec((LPCSTR) FileSpec);
	CString csTemp;
	CString csTempFileName;

	if (Count) {
		m_FileList.RemoveAll();
		if (Count > 1 || (Count == 1 && FileSpec.FindOneOf("*?") >= 0)) {
			// multiple files or a single match to a wild card specifier
			CFileFind finder;
			BOOL bWorking = finder.FindFile(FileSpec);
			while (bWorking) {
				bWorking = finder.FindNextFile();
				m_FileList.Add(finder.GetFilePath());
			}
		}
		else {		// only 1 file matched FileSpec and no wildcard...might be list file or single model
			// check for text file list...must have ".txt" extension
			csTemp = FileSpec;
			csTemp.MakeLower();
			if (csTemp.Find(".txt") >= 0) {
				// open text file and read file name
				CDataFile lst(FileSpec);
				if (lst.IsValid()) {
					char buf[1024];
					Count = 0;
					while (lst.NewReadASCIILine(buf)) {
						csTempFileName = buf;
						csTempFileName.TrimLeft(" ");
						csTempFileName.TrimRight(" ");
						m_FileList.Add(csTempFileName);
						Count ++;
					}
				}
				else {
					Count = 0;
				}
			}
			else {
				// only 1 file directly specified (no wild card characters)
				m_FileList.Add(FileSpec);
			}
		}
	}

	return(Count);
}


void CTerrainSet::Destroy()
{
	if (m_OverviewValid)
		DestroyOverview();

	if (m_HaveInternalModel) {
		m_InternalModel.Destroy();
		m_HaveInternalModel = FALSE;
		m_InternalModelResolutionWasReduced = FALSE;
	}

	Initialize();
}

BOOL CTerrainSet::CreateOverview()
{
	int i;

	// get rid of existing info...if any
	DestroyOverview();

	// look at models
	if (!m_FileSpec.IsEmpty()) {
		// we have a file specifier that defines the set of .dtm files
		m_FileCount = ExpandFileSpecToList(m_FileSpec);
		if (m_FileCount) {
			RearrangeFileList(1);
			m_ModelHeaderList = new PlansDTM[m_FileCount];
			if (m_ModelHeaderList) {
				m_ModelElevLoaded = new int[m_FileCount];
				m_ModelUsedForInternalModel = new int[m_FileCount];
				m_ModelWithinPreloadExtent = new int[m_FileCount];
				m_ModelResolutionMatchesFirst = new int[m_FileCount];

				if (m_ModelElevLoaded && m_ModelUsedForInternalModel && m_ModelWithinPreloadExtent) {
					// read all model headers...DTMs will be set up for patch access but file is not left open
					for (i = 0; i < m_FileCount; i++) {
						m_ModelHeaderList[i].LoadElevations(m_FileList[i]);		// default is patch access

						// set flag to FALSE...no elevation loaded
						m_ModelElevLoaded[i] = FALSE;

						// clear flag indicating model was not used for internal model
						m_ModelUsedForInternalModel[i] = FALSE;

						// clear flag indicating model was within preload extent...we don't really have a preload extent
						// but you don't have to preload data...use patch access for everything...not best idea but possible
						m_ModelWithinPreloadExtent[i] = TRUE;

						// mark model if the resolution matches the first model...this will be used in the logic
						// for interpolation and when building an internal model
						//
						// ran into models with very slight differences in resolution so need a tolerance
						if (fabs(m_ModelHeaderList[i].ColumnSpacing() - m_ModelHeaderList[0].ColumnSpacing()) <= 0.001)
							m_ModelResolutionMatchesFirst[i] = TRUE;
						else
							m_ModelResolutionMatchesFirst[i] = FALSE;
					}
					m_OverviewValid = TRUE;
				}
				else {
					if (m_ModelElevLoaded)
					delete[] m_ModelElevLoaded;

					if (m_ModelUsedForInternalModel)
					delete[] m_ModelUsedForInternalModel;

					if (m_ModelWithinPreloadExtent)
						delete[] m_ModelWithinPreloadExtent;

					if (m_ModelResolutionMatchesFirst)
						delete[] m_ModelResolutionMatchesFirst;

					m_OverviewValid = FALSE;
				}
			}
		}
		else {
			// no files
			m_OverviewValid = FALSE;
		}
	}
	else if (m_FileCount > 0) {
		RearrangeFileList(1);

		// have a manually created list but no file specifier
		m_ModelHeaderList = new PlansDTM[m_FileCount];
		if (m_ModelHeaderList) {
			m_ModelElevLoaded = new int[m_FileCount];
			m_ModelUsedForInternalModel = new int[m_FileCount];
			m_ModelWithinPreloadExtent = new int[m_FileCount];
			m_ModelResolutionMatchesFirst = new int[m_FileCount];

			if (m_ModelElevLoaded && m_ModelUsedForInternalModel && m_ModelWithinPreloadExtent) {
				// read all model headers...DTMs will be set up for patch access but file is not left open
				for (i = 0; i < m_FileCount; i ++) {
					m_ModelHeaderList[i].LoadElevations(m_FileList[i]);		// default is patch access

					// set flag to FALSE...no elevation loaded
					m_ModelElevLoaded[i] = FALSE;

					// clear flag indicating model was not used for internal model
					m_ModelUsedForInternalModel[i] = FALSE;

					// clear flag indicating model was within preload extent...we don't really have a preload extent
					// but you don't have to preload data...use patch access for everything...not best idea but possible
					m_ModelWithinPreloadExtent[i] = TRUE;

					// mark model if the resolution matches the first model...this will be used in the logic
					// for interpolation and when building an internal model
					if (m_ModelHeaderList[i].ColumnSpacing() == m_ModelHeaderList[0].ColumnSpacing())
						m_ModelResolutionMatchesFirst[i] = TRUE;
					else
						m_ModelResolutionMatchesFirst[i] = FALSE;
				}
				m_OverviewValid = TRUE;
			}
			else {
				if (m_ModelElevLoaded)
					delete[] m_ModelElevLoaded;

				if (m_ModelUsedForInternalModel)
					delete[] m_ModelUsedForInternalModel;

				if (m_ModelWithinPreloadExtent)
					delete[] m_ModelWithinPreloadExtent;

				if (m_ModelResolutionMatchesFirst)
					delete[] m_ModelResolutionMatchesFirst;

				m_OverviewValid = FALSE;
			}
		}
	}
	else {
		// no files
		m_OverviewValid = FALSE;
	}

	// if the overview is valid, check to see if there are gaps within the model extent...this is focused on problem with 3DEP DEMs
	if (m_OverviewValid) {
		// not sure how to do this...I first thought you could compute the area of the individual tiles and compare to the area within the overall extent but this
		// would only work with rectangular blocks of tiles which we rarely have. This approach would also fail if you have a background DTM. In this case, the 
		// sum of the tile areas would exceed the area within the overall extent.
		//
		// This step might not be necessary if there is a simple, fast way to test for valid value around a specific location implemented in InterpolateElev()
		//
	}
	m_LastModelForInterpolation = -1;

	return(m_OverviewValid);
}

void CTerrainSet::DestroyOverview()
{
	if (m_OverviewValid && m_ModelHeaderList) {
		// unload data
		for (int i = 0; i < m_FileCount; i ++) {
			m_ModelHeaderList[i].Destroy();
		}

		// delete list
		delete [] m_ModelHeaderList;
		m_ModelHeaderList = NULL;

		// delete flags for loaded elevations
		delete [] m_ModelElevLoaded;
		m_ModelElevLoaded = NULL;

		delete[] m_ModelUsedForInternalModel;
		m_ModelUsedForInternalModel = NULL;

		delete[] m_ModelWithinPreloadExtent;
		m_ModelWithinPreloadExtent = NULL;
	
		if (m_ModelResolutionMatchesFirst)
			delete[] m_ModelResolutionMatchesFirst;
	}

	if (m_HaveInternalModel) {
		m_InternalModel.Destroy();
		m_HaveInternalModel = FALSE;
		m_InternalModelResolutionWasReduced = FALSE;
	}

	m_OverviewValid = FALSE;
}

BOOL CTerrainSet::RectanglesIntersect(double MinX1, double MinY1, double MaxX1, double MaxY1, double MinX2, double MinY2, double MaxX2, double MaxY2, double Expand)
{
	// test to see if the rectangles defined by the corners overlap...must actually overlap, not just touch
	if (MinX1 > (MaxX2 + Expand))
		return(FALSE);
	if (MaxX1 < (MinX2 - Expand))
		return(FALSE);
	if (MinY1 > (MaxY2 + Expand))
		return(FALSE);
	if (MaxY1 < (MinY2 - Expand))
		return(FALSE);

	return(TRUE);
}

BOOL CTerrainSet::RectanglesIntersectOrTouch(double MinX1, double MinY1, double MaxX1, double MaxY1, double MinX2, double MinY2, double MaxX2, double MaxY2, double Expand)
{
	// test to see if the rectangles defined by the corners overlap...even if only exact overlap...edge to edge
	if (MinX1 >= (MaxX2 + Expand))
		return(FALSE);
	if (MaxX1 <= (MinX2 - Expand))
		return(FALSE);
	if (MinY1 >= (MaxY2 + Expand))
		return(FALSE);
	if (MaxY1 <= (MinY2 - Expand))
		return(FALSE);

	return(TRUE);
}

void CTerrainSet::PreLoadData(double MinX, double MinY, double MaxX, double MaxY, BOOL CreateInternalModel, BOOL KeepOriginalDTMCellSize, BOOL AccumulateValues)
{
	// RE: AccumulateValues
	// if we are loading several models, the accumlation will be handled when interpolating values
	// if we have an internal model, we need to accumulate values as we build the internal model
	if (m_OverviewValid) {
		// if coords are all -1.0, load all models
		// no chance to use internal model because we don't have a valid extent
		if (MinX == -1.0 && MinY == -1.0 && MaxX == -1.0 && MaxY == -1.0) {
			for (int i = 0; i < m_FileCount; i ++) {
				// clear flag indicating elevations are loaded
				m_ModelElevLoaded[i] = FALSE;
				m_ModelUsedForInternalModel[i] = FALSE;
				m_ModelWithinPreloadExtent[i] = FALSE;

				// try to load elevation data
				m_ModelHeaderList[i].LoadElevations(m_FileList[i], TRUE, FALSE);
				if (!m_ModelHeaderList[i].IsValid()) {
					// set up for patch access
					m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
				}
				else
					m_ModelElevLoaded[i] = TRUE;

				m_ModelWithinPreloadExtent[i] = TRUE;		// always since extent was not given
			}
		}
		else {
			// see if we are creating an internal model
			if (CreateInternalModel && m_FileCount) {
				int i;

				// compute the area of the requested extent and the area of the loaded models
				double OverallForSet_MinX, OverallForSet_MaxX, OverallForSet_MinY, OverallForSet_MaxY;
				double OverallForSet_Area, RequestedArea;
				GetOverallExtent(OverallForSet_MinX, OverallForSet_MaxX, OverallForSet_MinY, OverallForSet_MaxY);
				OverallForSet_Area = (OverallForSet_MaxX - OverallForSet_MinX) * (OverallForSet_MaxY - OverallForSet_MinY);
				RequestedArea = (MaxX - MinX) * (MaxY - MinY);

				// count number of models in memory...we shouldn't have any models loaded
				int LoadedModels = 0;
				for (i = 0; i < m_FileCount; i ++) {
					if (AreModelElevationsInMemory(i))
						LoadedModels ++;
				}

				// only create the internal model if the requested area is smaller than the loaded area or some models couldn't be loaded
				// into memory
				//
				// I originally had this set to create the internal model only if the requested area was smaller than 75% of the overall area
				// for the model set but we were running into situations where you could not load all of the separate models and were
				// left with some models being used from disk. This was VERY slow...on a test with 5 dtms (4 loaded into memory and 1 from disk)
				// run time was ~80 times slower (72 versus 5672 seconds).
//				if (RequestedArea < OverallForSet_Area * 0.75 || LoadedModels < m_FileCount) {		// 75% of overall model area
				if (RequestedArea < OverallForSet_Area * 1.0 || LoadedModels < m_FileCount) {
					if (m_HaveInternalModel) {
						m_InternalModel.Destroy();
						m_HaveInternalModel = FALSE;
						m_InternalModelResolutionWasReduced = FALSE;
					}

					// compute modified extent and number of rows/cols for internal model
					double IM_OriginX, IM_OriginY;
					double IM_MaxX, IM_MaxY;
					double IM_CellWidth, IM_CellHeight;
					double X, Y, Elev;
					double InternalValue;
					long IM_Columns, IM_Points;
					int Attempts = 0;

					// compute model corners...add 1 cell to make sure there aren't any edge problems
					IM_CellWidth = m_ModelHeaderList[0].ColumnSpacing();
					IM_CellHeight = m_ModelHeaderList[0].PointSpacing();

					// try to build an internal model...use the highest resolution of the input dtms to start with and then halve the resolution 
					// until we successfully create an internal model
					//
					// try up to 10 times
					do {
						Attempts ++;

						IM_OriginX = MinX - IM_CellWidth;
						IM_OriginY = MinY - IM_CellHeight;
						IM_MaxX = MaxX + IM_CellWidth;
						IM_MaxY = MaxY + IM_CellHeight;

						// adjust origin to be a multiple of the cell size
						// 3/14/2022 fix logic so it works with negative X and Y values
						if (fmod(IM_OriginX, IM_CellWidth) > 0.0) {
							IM_OriginX = IM_OriginX - fmod(IM_OriginX, IM_CellWidth);
						}
						else if (fmod(IM_OriginX, IM_CellWidth) < 0.0) {
							IM_OriginX = IM_OriginX - (IM_CellWidth + fmod(IM_OriginX, IM_CellWidth));
						}

						if (fmod(IM_OriginY, IM_CellHeight) > 0.0) {
							IM_OriginY = IM_OriginY - fmod(IM_OriginY, IM_CellHeight);
						}
						else if (fmod(IM_OriginY, IM_CellHeight) < 0.0) {
							IM_OriginY = IM_OriginY - (IM_CellHeight + fmod(IM_OriginY, IM_CellHeight));
						}

						if (fmod(IM_MaxX, IM_CellWidth) > 0.0) {
							IM_MaxX = IM_MaxX + (IM_CellWidth - fmod(IM_MaxX, IM_CellWidth));
						}
						else if (fmod(IM_MaxX, IM_CellWidth) < 0.0) {
							IM_MaxX = IM_MaxX - fmod(IM_MaxX, IM_CellWidth);
						}

						if (fmod(IM_MaxY, IM_CellHeight) > 0.0) {
							IM_MaxY = IM_MaxY + (IM_CellHeight - fmod(IM_MaxY, IM_CellHeight));
						}
						else if (fmod(IM_MaxY, IM_CellHeight) < 0.0) {
							IM_MaxY = IM_MaxY - fmod(IM_MaxY, IM_CellHeight);
						}

						// compute the number of rows/cols in the internal model
						IM_Columns = (int) ((IM_MaxX - IM_OriginX + IM_CellWidth / 2.0) / IM_CellWidth) + 1;
						IM_Points = (int) ((IM_MaxY - IM_OriginY + IM_CellHeight / 2.0) / IM_CellHeight) + 1;

						// create the model and allocate memory for elevation data
						if (m_InternalModel.CreateNewModel(IM_OriginX, IM_OriginY, IM_Columns, IM_Points, IM_CellWidth, IM_CellHeight, m_ModelHeaderList[0].GetXYUnits(), m_ModelHeaderList[0].GetElevationUnits(), m_ModelHeaderList[0].GetZBytes(), m_ModelHeaderList[0].CoordinateSystem(), m_ModelHeaderList[0].CoordinateZone(), m_ModelHeaderList[0].HorizontalDatum(), m_ModelHeaderList[0].VerticalDatum())) {
							// loop through models in list and interpolate values for internal model...do this 1 model at a time to minimize memory use
							//
							// to deal with a model list that has models of different resolutions, work with the models that match
							// the resolution of the first model first, fill small holes, and then interpolate values from other models
							int j, k, l;
							for (i = 0; i < m_FileCount; i ++) {

								// only work with models that match the first model resolution...we will use the others later
								if (!m_ModelResolutionMatchesFirst[i])
									continue;

								// clear flag indicating elevations are loaded
								m_ModelElevLoaded[i] = FALSE;
								m_ModelUsedForInternalModel[i] = FALSE;
								m_ModelWithinPreloadExtent[i] = FALSE;

								// see if the model overlaps area of interest...if there is no overlap move to next model
								if (RectanglesIntersect(MinX, MinY, MaxX, MaxY, m_ModelHeaderList[i].OriginX(), m_ModelHeaderList[i].OriginY(), m_ModelHeaderList[i].OriginX() + m_ModelHeaderList[i].Width(), m_ModelHeaderList[i].OriginY() + m_ModelHeaderList[i].Height(), m_ModelHeaderList[0].PointSpacing() * 4.0)) {
									// try to load elevation data
									m_ModelHeaderList[i].LoadElevations(m_FileList[i], TRUE, FALSE);
									if (!m_ModelHeaderList[i].IsValid()) {
										// set up for patch access
										m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
									}
									else {
										m_ModelElevLoaded[i] = TRUE;
									}

									m_ModelUsedForInternalModel[i] = TRUE;
									m_ModelWithinPreloadExtent[i] = TRUE;

									// go through internal model and interpolate values for all cells using loaded model...
									// this does interpolation because the resolution may not match the source models due to memory limitations
									X = IM_OriginX;
									for (j = 0; j < IM_Columns; j ++) {
										Y = IM_OriginY;
										for (k = 0; k < IM_Points; k ++) {
											if (X >= m_ModelHeaderList[i].OriginX() && Y >=  m_ModelHeaderList[i].OriginY() && X <=  (m_ModelHeaderList[i].OriginX() +  m_ModelHeaderList[i].Width()) && Y <=  (m_ModelHeaderList[i].OriginY() +  m_ModelHeaderList[i].Height())) {
												if (AccumulateValues) {
													if (SetIsRasterData())
														Elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
													else
														Elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

													if (Elev >= 0.0) {
														InternalValue = m_InternalModel.GetGridElevation(j, k);
														if (InternalValue >= 0.0)
															m_InternalModel.SetInternalElevationValue(j, k, Elev + InternalValue);
														else
															m_InternalModel.SetInternalElevationValue(j, k, Elev);
													}
												}
												else {
													if (m_InternalModel.GetGridElevation(j, k) < 0.0) {
														if (SetIsRasterData())
															Elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
														else
															Elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

														m_InternalModel.SetInternalElevationValue(j, k, Elev);
													}
												}
											}
											Y += m_InternalModel.PointSpacing();
										}
										X += m_InternalModel.ColumnSpacing();
									}

									// unload model...set up for patch access
									m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
									m_ModelElevLoaded[i] = FALSE;
								}
							}

							m_HaveInternalModel = TRUE;

							if (Attempts != 1)
								m_InternalModelResolutionWasReduced = TRUE;

							// internal model was created...using models with resolution matching the first model (could be all of them)...
							//
							// with USGS DEMs, there can be 1 cell gaps that span the entire width/height of the model in memory. Ideally, these gaps could be filled
							// using the FillHoles() function in the PlansDTM class. However, this class is designed with special logic to prevent filling when the
							// valid data has gaps that extend to the edge of the model. I think this is problematic for the USGS DEM case because the filling would not happen
							// where it is needed along the full width/height gap.
							//
							// So we need special code to fill linear holes...should be pretty easy to fill along columns and then along rows. Just need to make sure there is valid data
							// to use for the interpolation
							//
							// 3/2/2022
							// while putting together an example to test the gap filling logic when you have a coarser model in the list, I found another problem case. If an edge of the tile extent
							// falls within the gap between fine models, there won't be values that straddle the void so no interpolation happens. For the example, the bottom edge was between
							// fine models so the first (maybe second as well) points in each column had NODATA values.

//							m_InternalModel.WriteModel("c:\\temp\\internalDTM_1.dtm");

							// scan through columns and rows and interpolate values to fill gaps...set a maximum span that will be filled
							long FirstValidValue, LastValidValue;
							BOOL HaveBadValue;
							BOOL FirstPointBad;
							BOOL LastPointBad;
							double Value;
							long MaxFillSpan = 3;

							// scan columns
							for (j = 0; j < m_InternalModel.Columns(); j++) {
								FirstValidValue = LastValidValue = -1;
								HaveBadValue = FirstPointBad = LastPointBad = FALSE;
								for (k = 0; k < m_InternalModel.Rows(); k++) {
									Value = m_InternalModel.GetGridElevation(j, k);
									if (Value >= 0.0) {
										if (!HaveBadValue)
											FirstValidValue = k;
										else {
											LastValidValue = k;
										}
									} 
									else {
										// see if we have a valid elevation to work with
										if (FirstValidValue != -1) {
											HaveBadValue = TRUE;
										}
										else {
											// test to see if we are on the first or last point in column
											if (k == 0)
												FirstPointBad = TRUE;

											if (k == m_InternalModel.Rows() - 1)
												LastPointBad = TRUE;
										}
									}

									// see if we have a span of bad values
									if (FirstValidValue != -1 && LastValidValue != -1) {
										// check to see if span is short enough to fill...if not, dont fill
										if ((LastValidValue - FirstValidValue - 1) <= MaxFillSpan) {
											// set invalid values to average of first and last valid values
											Value = (m_InternalModel.GetGridElevation(j, FirstValidValue) + m_InternalModel.GetGridElevation(j, LastValidValue)) / 2.0;
											for (l = FirstValidValue + 1; l < LastValidValue; l++)
												m_InternalModel.SetInternalElevationValue(j, l, Value);

											// reset varibles and continue
											FirstValidValue = LastValidValue = -1;
											HaveBadValue = FALSE;
										}
									}

									// see if first point is bad and we have a good point after the first
									if (FirstPointBad && FirstValidValue != -1) {
										// check span
										if (FirstValidValue <= MaxFillSpan) {
											Value = m_InternalModel.GetGridElevation(j, FirstValidValue);
											for (l = 0; l < FirstValidValue; l++)
												m_InternalModel.SetInternalElevationValue(j, l, Value);
										}
										// don't reset FirstValidValue as we could be starting a new run with the next point bad
										FirstPointBad = FALSE;
									}

									// see if last value is bad and we have a good point before the last
									if (LastPointBad && FirstValidValue != -1) {
										// check span
										if ((m_InternalModel.Rows() - FirstValidValue - 1) <= MaxFillSpan) {
											Value = m_InternalModel.GetGridElevation(j, FirstValidValue);
											for (l = FirstValidValue + 1; l < m_InternalModel.Rows(); l++)
												m_InternalModel.SetInternalElevationValue(j, l, Value);
										}
										// don't reset FirstValidValue as we could be starting a new run with the next point bad
										LastPointBad = FALSE;
									}
								}
							}

							// scan rows
							for (k = 0; k < m_InternalModel.Rows(); k++) {
								FirstValidValue = LastValidValue = -1;
								HaveBadValue = FirstPointBad = LastPointBad = FALSE;
								for (j = 0; j < m_InternalModel.Columns(); j++) {
									Value = m_InternalModel.GetGridElevation(j, k);
									if (Value >= 0.0) {
										if (!HaveBadValue)
											FirstValidValue = j;
										else {
											LastValidValue = j;
										}
									}
									else {
										// see if we have a valid elevation to work with
										if (FirstValidValue != -1) {
											HaveBadValue = TRUE;
										}
										else {
											// test to see if we are on the first or last point in column
											if (j == 0)
												FirstPointBad = TRUE;

											if (j == m_InternalModel.Columns() - 1)
												LastPointBad = TRUE;
										}
									}

									// see if we have a span of bad values
									if (FirstValidValue != -1 && LastValidValue != -1) {
										// check to see if span is short enough to fill...if not, dont fill
										if ((LastValidValue - FirstValidValue - 1) <= MaxFillSpan) {
											// set invalid values to average of first and last valid values
											Value = (m_InternalModel.GetGridElevation(FirstValidValue, k) + m_InternalModel.GetGridElevation(LastValidValue, k)) / 2.0;
											for (l = FirstValidValue + 1; l < LastValidValue; l++)
												m_InternalModel.SetInternalElevationValue(l, k, Value);

											// reset varibles and continue
											FirstValidValue = LastValidValue = -1;
											HaveBadValue = FALSE;
										}
									}

									// see if first point is bad and we have a good point after the first
									if (FirstPointBad && FirstValidValue != -1) {
										// check span
										if (FirstValidValue <= MaxFillSpan) {
											Value = m_InternalModel.GetGridElevation(FirstValidValue, k);
											for (l = 0; l < FirstValidValue; l++)
												m_InternalModel.SetInternalElevationValue(l, k, Value);
										}
										// don't reset FirstValidValue as we could be starting a new run with the next point bad
										FirstPointBad = FALSE;
									}

									// see if last value is bad and we have a good point before the last
									if (LastPointBad && FirstValidValue != -1) {
										// check span
										if ((m_InternalModel.Columns() - FirstValidValue - 1) <= MaxFillSpan) {
											Value = m_InternalModel.GetGridElevation(FirstValidValue, k);
											for (l = FirstValidValue + 1; l < m_InternalModel.Columns(); l++)
												m_InternalModel.SetInternalElevationValue(l, k, Value);
										}
										// don't reset FirstValidValue as we could be starting a new run with the next point bad
										LastPointBad = FALSE;
									}
								}
							}

//							m_InternalModel.WriteModel("c:\\temp\\internalDTM_2.dtm");
							//							m_InternalModel.WriteModel("c:\\temp\\internalDTM_nogaps.dtm");

							// at this point we may still have areas that need populated from coarse resolution models so we loop
							// again and see if we can fill more cells. Typically this is only needed when USGS 10m models are used
							// to provide background surface values.
							for (i = 0; i < m_FileCount; i++) {
								// only work with models that DON'T match the first model resolution
								if (m_ModelResolutionMatchesFirst[i])
									continue;

								// clear flag indicating elevations are loaded
								m_ModelElevLoaded[i] = FALSE;
								m_ModelUsedForInternalModel[i] = FALSE;
								m_ModelWithinPreloadExtent[i] = FALSE;

								// see if the model overlaps area of interest...if there is no overlap move to next model
								if (RectanglesIntersect(MinX, MinY, MaxX, MaxY, m_ModelHeaderList[i].OriginX(), m_ModelHeaderList[i].OriginY(), m_ModelHeaderList[i].OriginX() + m_ModelHeaderList[i].Width(), m_ModelHeaderList[i].OriginY() + m_ModelHeaderList[i].Height(), m_ModelHeaderList[0].PointSpacing() * 4.0)) {
									// try to load elevation data
									m_ModelHeaderList[i].LoadElevations(m_FileList[i], TRUE, FALSE);
									if (!m_ModelHeaderList[i].IsValid()) {
										// set up for patch access
										m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
									}
									else {
										m_ModelElevLoaded[i] = TRUE;
									}

									m_ModelUsedForInternalModel[i] = TRUE;
									m_ModelWithinPreloadExtent[i] = TRUE;

									// go through internal model and interpolate values for all cells using loaded model...
									// this does interpolation because the resolution may not match the source models due to memory limitations
									X = IM_OriginX;
									for (j = 0; j < IM_Columns; j++) {
										Y = IM_OriginY;
										for (k = 0; k < IM_Points; k++) {
											if (m_InternalModel.GetGridElevation(j, k) < 0.0) {
												if (X >= m_ModelHeaderList[i].OriginX() && Y >= m_ModelHeaderList[i].OriginY() && X <= (m_ModelHeaderList[i].OriginX() + m_ModelHeaderList[i].Width()) && Y <= (m_ModelHeaderList[i].OriginY() + m_ModelHeaderList[i].Height())) {
													if (AccumulateValues) {
														if (SetIsRasterData())
															Elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
														else
															Elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

														if (Elev >= 0.0) {
															InternalValue = m_InternalModel.GetGridElevation(j, k);
															if (InternalValue >= 0.0)
																m_InternalModel.SetInternalElevationValue(j, k, Elev + InternalValue);
															else
																m_InternalModel.SetInternalElevationValue(j, k, Elev);
														}
													}
													else {
														if (m_InternalModel.GetGridElevation(j, k) < 0.0) {
															if (SetIsRasterData())
																Elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
															else
																Elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

															m_InternalModel.SetInternalElevationValue(j, k, Elev);
														}
													}
												}
											}
											Y += m_InternalModel.PointSpacing();
										}
										X += m_InternalModel.ColumnSpacing();
									}

									// unload model...set up for patch access
									m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
									m_ModelElevLoaded[i] = FALSE;
								}
							}

//							m_InternalModel.WriteModel("c:\\temp\\internalDTM_3.dtm");

							// jump out of loop
							break;
						}
						else {
							// couldn't create internal model
							m_HaveInternalModel = FALSE;
							m_InternalModelResolutionWasReduced = FALSE;

							// if KeepOriginalDTMCellSize is TRUE, just fail after the first try and exit
							if (KeepOriginalDTMCellSize)
								break;

							// increase the cell size and try again...this should halve the resolution (1.4142 = sqrt(2))
							IM_CellWidth *= 1.4142;
							IM_CellHeight *= 1.4142;
						}
					} while (Attempts <= 10);
				}
			}
			else {
				for (int i = 0; i < m_FileCount; i ++) {
					// clear flag indicating elevations are loaded
					m_ModelElevLoaded[i] = FALSE;
					m_ModelUsedForInternalModel[i] = FALSE;
					m_ModelWithinPreloadExtent[i] = FALSE;

					// see if the model overlaps area of interest
					if (RectanglesIntersect(MinX, MinY, MaxX, MaxY, m_ModelHeaderList[i].OriginX(), m_ModelHeaderList[i].OriginY(), m_ModelHeaderList[i].OriginX() + m_ModelHeaderList[i].Width(), m_ModelHeaderList[i].OriginY() + m_ModelHeaderList[i].Height(), m_ModelHeaderList[0].PointSpacing() * 4.0)) {
						// try to load elevation data
						m_ModelHeaderList[i].LoadElevations(m_FileList[i], TRUE, FALSE);
						if (!m_ModelHeaderList[i].IsValid()) {
							// set up for patch access
							m_ModelHeaderList[i].LoadElevations(m_FileList[i]);
						}
						else
							m_ModelElevLoaded[i] = TRUE;

						m_ModelWithinPreloadExtent[i] = TRUE;
					}
				}
			}
		}
//		if (m_HaveInternalModel)
//			m_InternalModel.WriteModel("K:\\DensityTestData\\InternalModel.dtm");
	}
}

void CTerrainSet::UnloadData()
{
	if (m_OverviewValid) {
		for (int i = 0; i < m_FileCount; i ++) {
			// set up for patch access
			m_ModelHeaderList[i].LoadElevations(m_FileList[i]);

			// clear flag indicating elevations are loaded
			m_ModelElevLoaded[i] = FALSE;

			// clear flag indicating model is within preload extent
			m_ModelWithinPreloadExtent[i] = FALSE;
		}
	}
}

double CTerrainSet::InterpolateElev(double X, double Y, BOOL AccumulateValues, BOOL InterpolateOverGaps)
{
	double elev = -1.0;
	double AccumulatedValue = 0.0;

	// RE: AccumulateValues (needed when we use DTM files for raster data that may have overlap between one or more files e.g. return counts)
	// if we are working with an internal model, values should have already been accumulated
	// if working with a list of models and we are accumulating values (adding values from multiple grids), we add the values from all models covering the point
	if (m_OverviewValid) {
		if (m_HaveInternalModel) {
			if (m_SetIsRaster)
				elev = m_InternalModel.GetClosestPoint(X, Y);
			else
				elev = m_InternalModel.InterpolateElev(X, Y);

			AccumulatedValue = elev;
		}
		else {
			if (m_LastModelForInterpolation >= 0 && !AccumulateValues) {
				if (m_SetIsRaster)
					elev = m_ModelHeaderList[m_LastModelForInterpolation].GetClosestPoint(X, Y);
				else
					elev = m_ModelHeaderList[m_LastModelForInterpolation].InterpolateElev(X, Y);

				if (elev >= 0.0)
					return(elev);
			}

			// we didn't get an elevation from the last model used...
			// now try with all models that match the resolution of the first model
			for (int i = 0; i < m_FileCount; i++) {
				if (m_ModelWithinPreloadExtent[i] && m_ModelResolutionMatchesFirst[i]) {
					if (m_SetIsRaster)
						elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
					else
						elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

					if (elev >= 0.0) {
						// accumulate values
						AccumulatedValue += elev;

						// make sure that the model that was used is the highest resolution...without this, the logic will lock onto
						// a low resolution model and use it until a point is outside its extent
						// presumably the first model in the list is the highest resolution
						// if we find a valid elevation in a low-resolution model, we don't want to automatically use it for the next point
						if (m_ModelHeaderList[i].ColumnSpacing() == m_ModelHeaderList[0].ColumnSpacing()) {
							m_LastModelForInterpolation = i;
						}
						else {
							m_LastModelForInterpolation = -1;
						}
						if (!AccumulateValues)
							break;
					}
				}
			}

			if (elev < 0.0 && InterpolateOverGaps && !AccumulateValues) {
				// we don't have an internal model (in memory) and the elevation is not valid (-1)...this can be the case with USGS 3DEP DEMs converted to DTM format where there will be a gap in the
				// coverage between adjacent tiles. Interpolation in PlansDTM class will see points in this region as outside all model extents so no values are interpolated.
				//
				// use logic that interpolates across small gaps after trying all models with the same resolution as the first model
				elev = InterpolateInGap(X, Y, m_ModelHeaderList[0].ColumnSpacing());
			}
			
			if (elev < 0.0) {
				// we still don't have a valid elevation so work the list of models again but only use those that are coasrser than the first
				// this will allow the gap filling logic to work without influence from a coarser model used to provide background elevations
				//
				// we never want to update the last model used for interpolation in this case since it is coarser that the first model
				//
				// if there is no coarser model(s), this loop won't do anything
				for (int i = 0; i < m_FileCount; i++) {
					if (m_ModelWithinPreloadExtent[i] && !m_ModelResolutionMatchesFirst[i]) {
						if (m_SetIsRaster)
							elev = m_ModelHeaderList[i].GetClosestPoint(X, Y);
						else
							elev = m_ModelHeaderList[i].InterpolateElev(X, Y);

						if (elev >= 0.0) {
							// accumulate values
							AccumulatedValue += elev;

							// we have a valid elevation...exit loop
							if (!AccumulateValues)
								break;
						}
					}
				}
			}
		}
	}
	if (AccumulateValues)
		return(AccumulatedValue);
	else
		return(elev);
}

BOOL CTerrainSet::IsValid()
{
	return(m_OverviewValid);
}

int CTerrainSet::GetModelCount()
{
	if (m_OverviewValid)
		return(m_FileCount);

	return(0);
}

CString CTerrainSet::GetModelFileName(int index)
{
	CString csTemp;
	csTemp.Empty();

	if (m_OverviewValid && index < m_FileCount && index >= 0)
		csTemp = m_FileList[index];

	return(csTemp);
}

BOOL CTerrainSet::AreModelElevationsInMemory(int index)
{
	if (m_OverviewValid && index < m_FileCount && index >= 0)
		return(m_ModelElevLoaded[index]);

	return(FALSE);
}

BOOL CTerrainSet::GetOverallExtent(double &MinX, double &MinY, double &MaxX, double &MaxY)
{
	if (m_OverviewValid && m_FileCount > 0) {
		// intialize to etent of first model
		MinX = m_ModelHeaderList[0].OriginX();
		MinY = m_ModelHeaderList[0].OriginY();
		MaxX = m_ModelHeaderList[0].OriginX() + m_ModelHeaderList[0].Width();
		MaxY = m_ModelHeaderList[0].OriginY() + m_ModelHeaderList[0].Height();

		// go through models and get overall extent
		for (int i = 1; i < m_FileCount; i ++) {
			MinX = min(MinX, m_ModelHeaderList[i].OriginX());
			MinY = min(MinY, m_ModelHeaderList[i].OriginY());
			MaxX = max(MaxX, m_ModelHeaderList[i].OriginX() + m_ModelHeaderList[i].Width());
			MaxY = max(MaxY, m_ModelHeaderList[i].OriginY() + m_ModelHeaderList[i].Height());
		}

		return(TRUE);
	}
	return(FALSE);
}

BOOL CTerrainSet::AreModelsInteger()
{
	BOOL retcode = TRUE;

	if (m_OverviewValid && m_FileCount > 0) {
		for (int i = 0; i < m_FileCount; i ++) {
			if (m_ModelHeaderList[i].GetZBytes() >= 2) {
				retcode = FALSE;
				break;
			}
		}
	}
	return(retcode);
}

BOOL CTerrainSet::InternalModelIsValid()
{
	return (m_InternalModel.IsValid());
}

BOOL CTerrainSet::InternalModelResolutionWasReduced()
{
	return (m_InternalModelResolutionWasReduced);
}

CString CTerrainSet::InternalModelDescription()
{
	CString csTemp;
	csTemp = _T("Internal model is invalid");

	if (InternalModelIsValid()) {
		csTemp.Format("Origin (%.2lf,%.2lf) Cols: %li Rows: %li Cellwidth: %.2lf CellHeight: %.2lf", m_InternalModel.OriginX(), m_InternalModel.OriginY(), m_InternalModel.Columns(), m_InternalModel.Rows(), m_InternalModel.ColumnSpacing(), m_InternalModel.PointSpacing());
	}

	return(csTemp);
}

BOOL CTerrainSet::WasModelUsedForInternalModel(int index)
{
	if (InternalModelIsValid() && m_OverviewValid && index < m_FileCount && index >= 0)
		return(m_ModelUsedForInternalModel[index]);

	return(FALSE);
}

double CTerrainSet::GetFirstModelColumnSpacing()
{
	if (m_OverviewValid && m_FileCount > 0) {
		return(m_ModelHeaderList[0].ColumnSpacing());
	}
	return(-1.0);
}

double CTerrainSet::GetFirstModelPointSpacing()
{
	if (m_OverviewValid && m_FileCount > 0) {
		return(m_ModelHeaderList[0].PointSpacing());
	}
	return(-1.0);
}

BOOL CTerrainSet::AddFileToSet(LPCTSTR Filename)
{
	// make sure file exists
	if (_access(Filename, 0))
		return(FALSE);

	// add to list
	m_FileList.Add(Filename);

	m_FileCount ++;

	return(TRUE);
}

void CTerrainSet::RearrangeFileList(int Order)
{
	int i;
	PlansDTM dtm;
	TILEINFO* ti = NULL;
	
	// sort the file list based on criteria
	if (m_FileCount) {
		TILEINFO* ti = new TILEINFO[m_FileCount];

		if (ti) {
//printf("Pre-sort:\n");
			for (i = 0; i < m_FileCount; i ++) {
				// read the DTM header and get cell area
				dtm.Open(m_FileList[i]);

				ti[i].Index = i;
				ti[i].FileName = m_FileList[i];
				ti[i].CellArea = dtm.PointSpacing() * dtm.ColumnSpacing();

//printf("%s\n", m_FileList[i]);
			}
			// sort the list based on cell size
			qsort(ti, m_FileCount, sizeof(TILEINFO), CompareDTMs);

			// delete original file list and recreate using the sorted order
			m_FileList.RemoveAll();

			for (i = 0; i < m_FileCount; i ++) {
				m_FileList.Add(ti[i].FileName);
			}

			// delete tile info
			delete [] ti;

//printf("\nPost-sort:\n");
//for (i = 0; i < m_FileCount; i ++)
//	printf("%s\n", m_FileList[i]);
		}
	}
}

BOOL CTerrainSet::ModelOverlapsExtent(int Index, double MinX, double MinY, double MaxX, double MaxY)
{
	if (m_OverviewValid && Index < m_FileCount && Index >= 0) {
		if (RectanglesIntersect(MinX, MinY, MaxX, MaxY, m_ModelHeaderList[Index].OriginX(), m_ModelHeaderList[Index].OriginY(), m_ModelHeaderList[Index].OriginX() + m_ModelHeaderList[Index].Width(), m_ModelHeaderList[Index].OriginY() + m_ModelHeaderList[Index].Height(), m_ModelHeaderList[0].PointSpacing() * 4.0))
			return(TRUE);
	}
	return(FALSE);
}

BOOL CTerrainSet::SetIsRasterData()
{
	return(m_SetIsRaster);
}

void CTerrainSet::InterpretDataAsRaster()
{
	// must be called after the class is initialized and before data is preloaded or interpolation functions are called
	m_SetIsRaster = TRUE;
}

// Fill in values when there is a small gap between models in the TerrainSet
double CTerrainSet::InterpolateInGap(double X, double Y, double SearchDistance = 2.0)
{
	if (m_FileCount) {
		// look out in 8 directions to see we can find valid values to use for interpolation
		double elev;
		double elevsum;
		double weightsum;
		double newelev;
		int k;
		unsigned char vector = 0;
		double values[8] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
		double weights[8] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
		int ncount = 0;

		// look left
		elev = InterpolateElev(X - SearchDistance, Y, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / SearchDistance;
			ncount++;
			vector |= 1;
		}

		// look right
		elev = InterpolateElev(X + SearchDistance, Y, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / SearchDistance;
			ncount++;
			vector |= 16;
		}

		// look up
		elev = InterpolateElev(X, Y + SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / SearchDistance;
			ncount++;
			vector |= 64;
		}

		// look down
		elev = InterpolateElev(X, Y - SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / SearchDistance;
			ncount++;
			vector |= 4;
		}

		// look left and up
		elev = InterpolateElev(X - SearchDistance, Y + SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / (SearchDistance * 1.4142);		// sqrt(2)
			ncount++;
			vector |= 128;
		}

		// look right and up
		elev = InterpolateElev(X + SearchDistance, Y + SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / (SearchDistance * 1.4142);		// sqrt(2)
			ncount++;
			vector |= 32;
		}

		// look left and down
		elev = InterpolateElev(X - SearchDistance, Y - SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / (SearchDistance * 1.4142);		// sqrt(2)
			ncount++;
			vector |= 2;
		}

		// look right and down
		elev = InterpolateElev(X + SearchDistance, Y - SearchDistance, FALSE, FALSE);
		if (elev > 0.0) {
			values[ncount] = elev;
			weights[ncount] = 1.0 / (SearchDistance * 1.4142);		// sqrt(2)
			ncount++;
			vector |= 8;
		}

		// we have to have valid cells on opposite sides of the target point to do interpolation
		if ((vector & 68) == 68 || (vector & 17) == 17 || (vector & 34) == 34 || (vector & 136) == 136) {
			if (ncount > 0) {
				// compute a distance-weighted average of the values
				elevsum = 0.0;
				weightsum = 0.0;
				for (k = 0; k < ncount; k++) {
					elevsum += (values[k] * weights[k]);
					weightsum += weights[k];
				}

				newelev = elevsum / weightsum;
			}

			return(newelev);
		}
	}

	return(-1.0);
}


BOOL CTerrainSet::IsModelWithinPreloadAea(int index)
{
	if (m_OverviewValid && index < m_FileCount && index >= 0)
		return(m_ModelWithinPreloadExtent[index]);

	return(FALSE);
}
