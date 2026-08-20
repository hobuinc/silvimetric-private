// gridmetrics.cpp : Defines the entry point for the console application.
//
//
// version 1.2  11/14/2006
// fixed logic so density was not calculated when there were no first returns in a cell...previously
// density would be calculcated and would result in a divide by zero error
//
// fixed logic to set countabove and counttotal to NODATA when there were no returns in a cell...previously
// counttotal was set to NODATA but countabove was set to 0...correct but misleading if you didn't look at
// both layers
//
// V1.4 added support for text file listing the data files
//
// V1.5 added ouput of XY location at center of cell to end of data record in csv file
//
// V1.6 added /grid:X,Y,W,H switch to force specific grid origin and grid size for metric output...can be 
// subset of data extent
//
// V1.7 fixed problem when the lower right corners of the coverage grid contain no data points...memory
// access problems in release builds caused by testing a value in Points[] using an index that is a point
// counter (not a point index)
//
// V1.8 fixed problem when trying to process very large data sets...memory allocation problem with windows
// was causing the program to crash before it could produce a meaningful error message. Problem seemed to
// show up under windows XP and Vista but not Win2k. Added code to check amount of available memory before
// to allocate memory for the point list.
//
// V1.9 fixed logic that set the extent of output grids
//
// V2.0 added noground option and fixed problem when specifying multiple data files on the command line.
// Only the first data file was recognized. noground option lets you compute the metrics when no bare-earth
// model is available. When using this option, omit the "groundfile" parameter.
//
// v2.1 added /align switch to align output to .dtm file extent
//
// V2.2 fixed logic used to determine if specified output extent and data extent overlapped. V2.0- had trouble
// when the extent completely contained the data extent or vice-versa. Problem only occurred when data tiles
// were indexed.
//
// V2.3 fixed the logic that caused the cell processing loop to exit early. The result was a .csv file
// that didn't have entries for all the cells in the coverage area.
//
// V2.4 fixed logic used to compute cover so that cover values are not computed when there aren't 
// enough point in the cell to compute other metrics. previous versions output cover value for the 
// wrong cell when there were too few points.
//
// V2.5 added buffer logic to help produce metrics over large data collections with no "seams"
// The buffer capability is best used in a batch processing mode where you can specify analysis
// tiles and use the list of all LIDAR data files in the acquisition. The logic will only use the returns
// within the analysis tile boundary (plus the buffer, if specified)
//
// V2.6 added option for multiple ground surfaces and enhanced buffer logic to correct problems with
// off-by-1 error in grid cell labeling and mismatches between CSV files and raster file headers.
// Also added /minht switch to use only returns above the specified height for all metrics. Prior
// to this change you could use the /outlier switch to use only returns above the ground but this
// resulted in bad cover estimates. The behavior with the /minht switch is to use all first returns
// for the cover calculation and only returns above the specified height for all other metrics.
//
// V2.8 6/8/2009 added /class:##,##,## switch to use only specific returns from LAS files with 
// classification values specifed.
//
// V2.81 7/14/2009
//		Fixed problem reading list files with a blank line at the end of the file. Previous version would hang.
//
// V2.90	8/7/2009
//		Added cover above mean elevation and elevation mode. Added new logic to trap memory allcation errors
//		when trying to load points and DTMs into memory.
//
//		changed output of cover metrics to run from 0 to 100 instead of 0 to 1. CloudMetrics outputs values 
//		ranging from 0 to 100 so GridMetrics needed to match.
//
// V3.00	10/16/2009
//		Modified logic so intensity metrics and elevation metrics are computed using a single run of GridMetrics
//		Output is maintained in separate files and the cover values area only saved in the file with elevation
//		metrics. The /nointensity switch was added so you can compute only the elevation metrics. Fuel model
//		application only happens on the pass when computing elevation metrics. The /intensity switch was removed
//		and testing for combinations of /fuel and /intensity were disabled
//
//		Also changed the order of the columns int the CSV output and added several new metrics so the output from
//		GridMetrics matches output from CloudMetrics
//
//		Added all the new variables to the /raster switch so you can produce a DTM or ASCII raster file for specific
//		metrics.
//
//		Added /topo:dist,lat switch to compute topographic attributes using the ground surface model. A new grid is
//		interpolated with cells that are dist- by dist- units. Latitude is assumed the same for the entire project area.
//
//		Modified logic used to create file names for raster ouput (/raster switch) when producing ASCII raster files
//		to use the extension ".asc" instead of ".dtm"
//
// 12/10/2009 GridMetrics V3.01
//	Corrected a problem that was causing cover values for cells with too few points to be set to the wrong NODATA value.
//	(-999900.00 instead of -9999.00). This was causing problems in ArcInfo.
//
// 12/18/2009 GridMetrics V3.01
//	Corrected problems with the cover above mean and mode results and most of the intensity metrics. All problems were
//	introduced in the 10/16/2009 changes. 
//
// 2/3/2010 GridMetrics V3.10
//	Added code to compute L moments and moment ratios. Added 7 columns each to elevation and intensity metrics output
//
// 2/18/2010 GridMetrics V3.11
//	Added /extent switch to force output to align with a DTM file but still adjust the output extent to be an even 
//	multiple of the grid cell size. This addition is very similar to the /align option but with /align, the output 
//	origin and size are not adjusted to align with a multiple of the cell size. You can use this option when computing 
//	metrics using data tiles along with the /buffer option to eliminate edge effects in the merged rasters of metrics.
//
// 3/5/2010 GridMetrics V3.12
//	Added support for /minht switch (same meaning as /htmin switch) to prevent mishapes when using command line options
//	from CloudMetrics
//
// 4/16/2010 GridMetrics V3.20
//	Modified the logic that handles ground models to use the CTerrainSet class and changed the behavior of this class
//	so it creates an in-memory ground model covering the analysis extent. This makes for more efficient ground model
//	handling when individual ground models are large and multiple models are needed to cover the analysis extent (which
//	is usually small compared to the individual ground model extents). This should prevent situation where ground models
//	are used from disk while interpolating ground elevations for individual returns.
//
// 5/27/2010 GridMetrics V3.21
//	Corrected a problem with the L moments metrics. When setting NODATA values for cells with too few points to compute
//	metrics, the L moment metrics were not correctly set to NODATA values. In general this should only affect runs with very
//	sparse point data or with very small cells.
//
// 8/18/2010 GridMetrics V3.22
//	Corrected a problem with the data extent computed when using the /class switch. Previous versions used all data points
//	to compute the extent instead of using only points with matching classification codes. In general, this won't have caused
//	problems except in cases where the classifaction code was linked to the spatial position of the data points. 
//
//	also enhanced the logic used to handle ground models in memory...affects several LTK programs
//
// 11/1/2010 GridMetrics V3.23
//	Added /id:identifier switch to all a user specified identifier that is included in the last column of the output data.
//	This is most useful when you want to combine output from a series of GridMetrics runs into a single CSV file but you
//	still want to identify the metrics with a specific area. The identifier cannot contain spaces.
//
// 11/10/2010 GridMetrics V3.24
//	Added command line parameter verification logic.
//
//	1/14/2011 GridMetrics V3.25
//	Restructured code to provide better organization and make it easier to maintain and distribute FUSION source code. Also removed
//	references to JPEG and TIFF image libraries (can be easily controlled using #define INCLUDE_OUTLIER_IMAGE_CODE) to reduce the
//	executable size and eliminate the need for the image code in the public distribution.
//
//	Added new options: /strata:[#,#,#,...] /intstrata:[#,#,#,...] /kde:[window, multiplier]
//
//	Added new metrics: Canopy relief ratio, height strata (elevation and intensity metrics), MAD_MED, MAD_MODE, #modes
//	from a kernal density function using the return heights, min/max mode values, min/max range
//
// 3/17/2011 GridMetrics V3.26
//	Fixed initialization problem with P05 and P95 layers when using the /raster option. NODATA areas were getting the wrong value.
//
//	Added more logic to check for access to data files to help get around problems opening data files while other activities on the
//	computer have locked the files or otherwise are preventing access. The new logic will try to open the data file set several times
//	with a 10 second delay between each attempt. From experience, this should give programs a chance to finish what they are doing with
//	the data file set. Most problems have been the result of virus scanning activities on Forest Service-imaged computers so others
//	may not be having the problems with large processing jobs where a few tiles fail.
//
//	5/4/2011 GridMetrics V3.26
//	Added proportion of returns in strata when using the /strata option.
//
//	8/31/2011 Gridmetrics V3.27
//	Changed the default behavior with ground models so that an internal (in memory) model is not automatically created. In previous versions
//	an internal model was being created and this process was taking a very long time so the overall processing times were going up by a 
//	factor of 10. Still not sure exactly why processing was so slow but eliminating the extra time needed to create the internal model
//	seems to speed up processing.
//
// 11/16/2011 GridMetrics V3.28
//	Fixed a problem when computing strata metrics using the /strata or /intstrata options. The decision to compute the strata metrics was being
//	made based on the number of returns above the minimum height for other metrics. This resulted in bad metrics for any cells that did not have
//	enough returns above the minimum height to compute the "normal" metrics.
//
// 11/29/2011 GridMetrics V3.29
//	Added an /nointdtm option to prevent creation of an internal DTM when processing using the /grid, /gridxy, and /align options. For some 
//	datasets, the creation of the internal model was resulting in a problem with the metrics along the top and right edges of the data extent.
//	For most uses, this option is not needed. The case where problems arose involved a small rectangular dataset and metrics computed using a relatively 
//	large cell size. The internal ground model was being created with 1 extra row and column but the extra row and column were not being populated
//	with valid elevations since they were technically outside the data coverage area. As a result, the returns along the top and right sides
//	of the data extent were not being correctly adjusted for height above ground. This caused the elevation metrics to have incorrect values
//	for the cells along the top and right sides of the extent.
//
//	Also corrected a problem when using the /class switch to compute metrics for points with the specified LAS classification codes. Only values
//	from 0-15 were being processed correctly. The change ensures that values from 0-31 are processed correctly.
//
// 1/6/2012 GridMetrics V3.29
//	Modified the values output for metrics when there are no points in a cell. Previous versions output 0 for some return count metrics 
//	and -9999 (NODATA) for others. Starting with this version, all metrics have NODATA values when there are no points in the cell.
//
//	Also fixed a problem with the counts of individual returns when there were too few returns in a cell. Previous version set all 
//	individual return counts to 0. Starting with this version, the individual return counts reflect the actual counts in the cell when there
//	are too few returns in the cell. The number of points needed to compute metrics is controlled by the /minpts command line switch.
//
//	Changed the sytnax description to indicate that the default minimum number of returns needed to compute metrics is 4. Previous versions
//	indicated that the minimum number of returns was 3.
//
// 2/29/2012 GridMetrics V3.30
//	Added mean quadratic elevation and mean cubic elevation to the metrics. These are added to the output just after the canopy relief ratio.
//	The calculation of the these metrics is described in the CloudMetrics section of the FUSION manual. The quadratic mean height/elevation
//	has been shown in several papers to be useful for model building.
//
//	3/26/2012 GridMetrics V3.31
//	added /rgb switch to compute intensity metrics using red, green, or blue color values from LAS version 1.2+ files
//	with RGB values for each return (point record formats 2, 3, & 5). LAS version is not actually checked because
//	some LAS writers do not correctly output the format version.
//
//	Metrics for the Intensity, Red, Green, and Blue color components are ouptut to files labeled to indicate their content.
//
//	8/22/2012 GridMetrics V3.32
//	Added support for LASLIB to read LAS and LAZ files. LASLIB replaces the original code I developed to read LAS files. The
//	change results in faster file reads (about 15%) for uncompressed files and adds support for compressed LAS (LAZ) data. 
//	Other formats are still read using my original code.
//
//	Problem with LASlib code: you must link to static MFC library which increases the executable size by 10x. Also trouble
//	writing LAS files in ClipData so I am putting the use of LASlib on hold until I have time to resolve some of these issues.
//
//	1/31/2013 GridMetrics V3.33
//	Corrected a problem with the cubic mean elevation metric for cells with no returns. The NODATA value being written was
//	incorrect. -9999.9 was being written instead of -9999
//
//	5/22/2013 GridMetrics V3.34
//	Added an overall curvature to the topographic metrics calculated when the /topo option is used. The methodology is the same
//	that is used in ArcGIS. All topographic attributes are computed as outline in: Zevenbergen, L.W. and Thorne, C.R. 1987.
//	Quantitative analysis of land surface topography. Earth Surface Processes and Landforms. 12(1): 47-56.
//
//	8/13/2013 GridMetrics V3.35
//	Modified the logic used when computing topographic metrics to expand the extent of the ground surface used for the calculations
//	to cover the full extent of the area defined by the larger of a buffer around the extent for metrics and the area defined by the 
//	extent for metrics and the topogrphic analysis cell size. Previous versions only considered the buffer and could end up computing
//	incorrect topographic metrics when the topographic cell size was larger than the buffer.
//
//	Also added support for LAZ format using the LASzip.dll from Martin Isenburg -- Rapidlasso. If the LASzip.dll is in the FUSION install
//	folder, it will be loaded and used providing support for LAS and LAZ formats. If the dll is not available, the old FUSION-based code will
//	be used to support LAS only...LAZ is not supported without the library.
//
//	2/28/2014 GridMetrics V3.36
//	Corrected a problem where a topographic metrics file was being produced when there were no points in the tile. The header for the file
//	contained invalid values for the number of rows and columns and caused CVS2GRID and MergeRaster to fail. Now no topographic metrics
//	file is output unless there are points within the processing area
//
//	3/28/2014 GridMetrics V3.37
//	Fixed a problem when using LAZ files. GridMetrics was reporting a "File access time delay loop" error when using LAZ files. The problem
//	was related to some code cleanup prior to the last release.
//
//	10/1/2014 GridMetrics V3.38
//	Modified the checks on available memory to use the amount of virtual memory instead of physical memory. This should improve stability.
//
//	11/26/2014 GridMetrics V3.39
//	Modified the /class option to recognize "~" in the first character of the list of classification codes to mean that the listed
//	codes should be excluded. The default behavior is to include only returns with the classification codes listed.
//
//	7/17/2015 GridMetrics V3.40
//	Corrected a problem with the Elev cubic mean when the return elevation was negative. Resulting value was -1.#IND in CSV outputs. The correction
//	means that the cubic mean metric is computed by taking the cube root of the sum of the absolute values of the cubed heights for returns in the 
//	cell. Previous version summed the cubed height (without the absolute value) so the result was different.
//
//	8/3/2105 GridMetrics V3.41
//	Corrected a problem with the /cellbuffer option. Previous version did not implement this option correctly and did not use logic to
//	computer metrics for a larger area and then clip the outputs. Error resulted in no buffer being used in the calculation process.
//
//	8/25/2015 GridMetrics V3.42
//	Corrected a problem with skewness, kurtosis, Lskewness, and Lkurtosis metrics when the values for all returns in a cell were the same. Previous
//	version output a value indicating a divide-by-zero error (-1.#IND". Now the NODATA value is output instead. This error was discovered while
//	processing data where almost all of the intensity values for all returns were the same. In these cases, skewness and kurtosis are undefined
//	because there is no "distribution" of values (they are all the same).
//
//	12/2/2015 GridMetrics V3.43
//	Corrected a problem that occurred if you used input files in LDA format and the /class option. Technically this combination isn't valid but
//	there was no testing to see if this combinations was used. The "fix" disables the effect of the /class option if any of the input files are
//	LDA format
//
//	5/18/2018 GridMetrics V3.44
//	Added the /ignoreoverlap switch to control use of the points flagged as overlap point in LAS V1.4+ format files.
//
//	8/6/2018 GridMetrics V3.45
//	Corrected a problem when determining the min/max values for X, Y & Z in data files. Variables for the max values were being initialized to a vary 
//	small positive number. This worked fine unless the coordinates were all negative. For negative coordinates, the maximum values reported were always 0.0.
//
//	For some programs, there was no problem but code was changed to provide consistent initial values when determining min/max values.
//
//	9/4/2018 GridMetrics V3.50
//	Major code changes to move to "modern" development environment (MS Visual Studio 2017). Lots of small code changes needed to change environments and prepare for 
//	64-bit version.
//
//	4/30/2019 GridMetrics V3.51
//	Changed the logic used to compute the mode intensity/elevation/height value. GridMetrics was crashing when all points in the cell had the same intensity/elevation/height. For lidar
//	data this was rare (maybe never) but for DAP-derived point clouds, this can happen. This may have also been a problem with very small cell sizes where the number of 
//	points in the cell is small.
//
//	6/7/2019 GridMetrics V3.60
//	Added a new metric: profile area. For details, see A simple and integrated approach for fire severity assessment using bi-temporal airborne LiDAR data, 
//	Int J.Appl Earth Obs Geoinforamtion, 78 (2019) : 25 - 38. In FUSION's implementation, percentile height values are normalized using the 99th percentile
//	height instead of the maximum height as in the paper. This was done to prevent problems when high outliers are present. In addition, the area is computed
//	using the percentile values (1% breaks) instead of fitting a polynomial to the data. In testing, I found very little difference between the areas computed
//	directly and those computed from a fitted curve and there are problems determining the best polynomial form (order) that works across a variety of structure
//	types.
//
//	6/19/2019 GridMetrics V3.61
//	Also corrected a problem when computing mode values for strata. There was the potential for a divide-by-zero error when there is only 1 point in the strata. This is an error
//	that should have shown up before but I'm not sure why it hasn't been a problem. It could be that the new development tools have more extensive exception handling capabilities.
//	The error only occurred with the strata mode calculation and not with the metrics for points in the entire cell.
//
//	7/23/2019 GridMetrics V3.62
//	Fixed a problem with the profile area metric. For some areas without data, a value of -9999.9 was being output instead of -9999.0. This creates scaling problems and analysis
//	problems. This was only occurring in tiles where there was no valid point data in the lower section of the tile. As soon as valid data is encountered in the tile, the correct
//	NODATA value is used.
//
// 2/25/2020 GridMetrics V3.63
//	Changed the logic used to compute the mode elevation and intensity values for strata. This is the same changed done in V3.61 to detect and prevent a divide-by-zero
//	error but I missed it for the strata calculations. The real problem was that the min/max values for the strata can be the same so their difference is zero. This
//	can happen if there is only 1 point in the strata or if the min/max just happen to be the same.
//
// 5/1/2020 GridMetrics V3.64
//	Added code to prevent problems with the calculation of profile area when points have negative heights or the 99th percentile is negative. Changes should
//	prevent large negative and positive values for profile area.
//
// 1/14/2021 GridMetrics V3.65
// Added the failnoz option to check ground surface files prior to computing metrics and fail if there are any problems. Without the option, metrics are computed using
// point elevations (instead of heights) in areas with bad or missing ground surface data.
//
// 1/19/2021 GridMetrics V3.66
// Added additional error messages when opening input data in verbose mode. The main focus is to capture cases where you are trying to read compressed point data (LAZ)
// format without having the laszip.dll or laszip64.dll available. Previous versions would just say that some files couldn't be opened without giving a reason.
//
// 1/3/2022 Gridmetrics V3.70
// Fixed a major problem with the intensity metrics when the /strata option was used. Some of these metrics were computed incorrectly in previous versions when using the 
// /strata option. The problem was obvious when evaulating values for the metrics. For the L-moment metrics, L1 was correct but L2, L3, and L4 could
// sometimes have negative values (not possible). Lcv, Lskew, and Lkurt were also incorrect. Percentile metrics for intensity would not be correct and values did not 
// increase going from P01 to P99 as they should. This problem only affected intensity metrics and only occurred when the /strata option was used.
//
// 3/14/2022 GridMetrics V3.71
// Fixed logic that computes the extent of the point data to correctly expand the grid for metrics whne X or Y values are negative. This happens in some projections such
// as web Mercator where X values for North America are negative. Previous code did not correctly align the output grid for metrics to be a multiple of the cell size.
//
// 5/17/2022 Gridmetrics V3.72
// Added logic that checks for valid point file formats and eliminates non-point files prior to the start of processing. This logic is designed to overcome problems
// with Windows and wildcard processing. For example, *.las will match files with the .las extension but also match files with the .lasx extension. This new logic
// can be turned off using the /skipfilechack option.
//
// 8/15/2023 GridMetrics V3.73
// Changed the logic used to compute variance and standard deviation to use a more mathematically robust algorithm. Previous code was not showing any problems but
// the method used for the statistics was prone to numerical instability. The new method is the same that was being used for CloudMetrics and strata metrics in
// GridMetrics. Not sure why the code wasn't changed when strata metrics were added.
//
// 8/12/2024 GridMetrics V3.74
//	Fixed the logic associated with the /rgb option to work correctly with LAS V1.4 files containing RGB information. Also added support for the NIR channel. Option
//	uses "N" to select the NIR channel for metrics.
//
//
#include "stdafx.h"
#include "gridmetrics.h"
#include <time.h>
#include "../../FUSION/versionID.h"
#include "plansdtm.h"
#include "lidardata_laslib.h"
#include "dataindex.h"
#include "filespec.h"
#include "argslib.h"
#include "array2dtemplate.h"
#include "TerrainSet.h"
#include "command_line_core_functions.h"
#include <float.h>
#include <math.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define		PROGRAM_NAME		"GridMetrics"
#define		PROGRAM_VERSION		3.74
#define		MINIMUM_CL_PARAMS	5		// special since the groundfile parameter is semi-optional
//			                              1         2         3         4         5         6         7         8         9         10
//			                     1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
#define		PROGRAM_DESCRIPTION	"Computes metrics for points falling within each cell of a grid"

#define		INTENSITY_VALUE		1
#define		ELEVATION_VALUE		2
#define		METRICS_DONE		3

#define		NUMBEROFBINS		64
#define		NUMBER_OF_CLASS_CODES		32
#define		MAX_NUMBER_OF_STRATA		32

// values used when computing matrics using RGB color values from LAS version 1.2 and newer
// used with m_RGBColor
#define		RGB_RED			1
#define		RGB_GREEN		2
#define		RGB_BLUE		3
#define		RGB_NIR			4

// define INCLUDE_OUTLIER_IMAGE_CODE to include code to create and outlier image. To actually create
// the images, you need to force m_CreateOutlierImage to TRUE as there is no corresponding switch
// you also have to add the following libraries to the linker options
// ..\..\common\image\tlib.lib ..\..\common\jpeg-7\libjpeg.lib 
//#define		INCLUDE_OUTLIER_IMAGE_CODE

#ifdef INCLUDE_OUTLIER_IMAGE_CODE
#include "image.h"
#endif	// INCLUDE_OUTLIER_IMAGE_CODE

typedef struct {
	int CellNumber;
	int ReturnNumber;
	float Elevation;
	//	float Intensity;
	float Value;
} PointRecord;

// global functions that are modified to create a new program
void GaussianKDE(float* PointData, int Pts, double BW, double SmoothWindow, int& ModeCount, double& MinMode, double& MaxMode);

BOOL RectanglesIntersect(double MinX1, double MinY1, double MaxX1, double MaxY1, double MinX2, double MinY2, double MaxX2, double MaxY2)
{
	// test to see if the rectangles defined by the corners overlap
	if (MinX1 > MaxX2)
		return(FALSE);
	if (MaxX1 < MinX2)
		return(FALSE);
	if (MinY1 > MaxY2)
		return(FALSE);
	if (MaxY1 < MinY2)
		return(FALSE);

	return(TRUE);
}

int comparevalue(const void *arg1, const void *arg2)
{
	if (((PointRecord*)arg1)->CellNumber < ((PointRecord*)arg2)->CellNumber)
		return(-1);
	else if (((PointRecord*)arg1)->CellNumber > ((PointRecord*)arg2)->CellNumber)
		return(1);
	else {
		// compare values...may be intensity or elevation
		if (((PointRecord*)arg1)->Value < ((PointRecord*)arg2)->Value)
			return(-1);
		else if (((PointRecord*)arg1)->Value > ((PointRecord*)arg2)->Value)
			return(1);
		else
			return(0);
	}
}

int comparefloat(const void *arg1, const void *arg2)
{
	// compare intensity values
	if (*(float*)arg1 < *(float*)arg2)
		return(-1);
	else if (*(float*)arg1 > *(float*)arg2)
		return(1);
	else
		return(0);
}

// comparison function for sorting...must be global to use qsort()
int comparePRint(const void *arg1, const void *arg2)
{
	if (((PointRecord*)arg1)->Value < (((PointRecord*)arg2)->Value))
		return(-1);
	else if (((PointRecord*)arg1)->Value > (((PointRecord*)arg2)->Value))
		return(1);
	else
		return(0);
}

// comparison function for sorting...must be global to use qsort()
int compareCellElev(const void *arg1, const void *arg2)
{
	if (((PointRecord*)arg1)->Elevation < (((PointRecord*)arg2)->Elevation))
		return(-1);
	else if (((PointRecord*)arg1)->Elevation > (((PointRecord*)arg2)->Elevation))
		return(1);
	else
		return(0);
}

/*
int compareintensity(const void *arg1, const void *arg2)
{
	if (((PointRecord*) arg1)->CellNumber < ((PointRecord*) arg2)->CellNumber)
		return(-1);
	else if (((PointRecord*) arg1)->CellNumber > ((PointRecord*) arg2)->CellNumber)
		return(1);
	else {
		// compare intensity values
		if (((PointRecord*) arg1)->Intensity < ((PointRecord*) arg2)->Intensity)
			return(-1);
		else if (((PointRecord*) arg1)->Intensity > ((PointRecord*) arg2)->Intensity)
			return(1);
		else
			return(0);
	}
}

int compareelevation(const void *arg1, const void *arg2)
{
	if (((PointRecord*) arg1)->CellNumber < ((PointRecord*) arg2)->CellNumber)
		return(-1);
	else if (((PointRecord*) arg1)->CellNumber > ((PointRecord*) arg2)->CellNumber)
		return(1);
	else {
		// compare elevation values
		if (((PointRecord*) arg1)->Elevation < ((PointRecord*) arg2)->Elevation)
			return(-1);
		else if (((PointRecord*) arg1)->Elevation > ((PointRecord*) arg2)->Elevation)
			return(1);
		else
			return(0);
	}
}
*/
/////////////////////////////////////////////////////////////////////////////
// The one and only application object

CWinApp theApp;

using namespace std;

char* ValidCommandLineSwitches = "rgb outlier class id minpts minht nocsv noground diskground failnoz nointdtm first nointensity fuel grid gridxy align extent buffer cellbuffer ascii topo raster strata intstrata kde ignoreoverlap ";

// global variables...not the best programming practice but helps with a "standard" template for command line utilities
CArray<CString, CString> m_DataFile;
CArray<CString, CString> m_GroundFiles;
int m_GroundFileCount;
BOOL m_GroundFromDisk;
CString m_FirstDataFile;
CString m_OutputFile;
double m_HeightBreak;
double m_CellSize;
int m_DataFileCount;
BOOL m_UseGround;
CString m_GroundFileName;
BOOL m_UseCanopy;
CString m_CanopyFileName;
BOOL m_UseFirstReturnsForDensity;
BOOL m_UseFirstReturnsForAllMetrics;
double m_CanopyHeightOffset;
int m_MinCellPoints;
//PlansDTM *m_GroundDTM;
CTerrainSet m_Terrain;
PlansDTM m_CanopyDTM;

BOOL m_TestTerrain;

double m_CustomOriginX;
double m_CustomOriginY;
double m_CustomGridWidth;
double m_CustomGridHeight;
double m_CustomMaxX;
double m_CustomMaxY;
BOOL m_UserGrid;
BOOL m_UserGridXY;
BOOL m_ForceAlignment;
BOOL m_ForceExtent;

BOOL m_CreateInternalDTM;

double m_MinHeightForMetrics;

BOOL m_AddBufferDistance;
BOOL m_AddBufferCells;
double m_BufferDistance;
int m_BufferCells;

int m_StatParameter;

BOOL m_Verbose;
BOOL m_CreateOutlierImage;

BOOL m_NoItensityMetrics;

BOOL m_DoRGBMetrics;
BOOL m_DoNIRMetrics;
int m_RGBColor;

BOOL m_UseGlobalIdentifier;
CString m_GlobalIdentifier;

BOOL m_NoCSVFile;

BOOL m_SkipOverlapPoints;

BOOL m_IncludeSpecificClasses;
int m_ClassCodes[NUMBER_OF_CLASS_CODES];
int m_NumberOfClassCodes;

BOOL m_DoKDEStats;
double m_KDEBandwidthMultiplier;
double m_KDEWindowSize;

BOOL m_DoHeightStrata;
BOOL m_DoHeightStrataIntensity;
int m_HeightStrataCount;
int m_HeightStrataIntensityCount;
double m_HeightStrata[MAX_NUMBER_OF_STRATA];
double m_HeightStrataIntensity[MAX_NUMBER_OF_STRATA];

BOOL m_ComputePA;

BOOL m_RasterFiles;
BOOL m_SaveASCIIRaster;
BOOL m_Create_PtCountGrid;
BOOL m_Create_PtCountTotalGrid;
BOOL m_Create_PtCountAboveGrid;
BOOL m_Create_MinGrid;
BOOL m_Create_MaxGrid;
BOOL m_Create_MeanGrid;
BOOL m_Create_ModeGrid;
BOOL m_Create_StdDevGrid;
BOOL m_Create_VarianceGrid;
BOOL m_Create_CVGrid;
BOOL m_Create_SkewnessGrid;
BOOL m_Create_KurtosisGrid;
BOOL m_Create_AADGrid;
BOOL m_Create_P01Grid;
BOOL m_Create_P05Grid;
BOOL m_Create_P10Grid;
BOOL m_Create_P20Grid;
BOOL m_Create_P25Grid;
BOOL m_Create_P30Grid;
BOOL m_Create_P40Grid;
BOOL m_Create_P50Grid;
BOOL m_Create_P60Grid;
BOOL m_Create_P70Grid;
BOOL m_Create_P75Grid;
BOOL m_Create_P80Grid;
BOOL m_Create_P90Grid;
BOOL m_Create_P95Grid;
BOOL m_Create_P99Grid;
BOOL m_Create_IQGrid;
BOOL m_Create_90m10Grid;
BOOL m_Create_95m05Grid;
BOOL m_Create_DensityGrid;
BOOL m_Create_DensityMeanGrid;
BOOL m_Create_DensityModeGrid;
BOOL m_Create_DensityCell;

BOOL m_Create_R1Count;
BOOL m_Create_R2Count;
BOOL m_Create_R3Count;
BOOL m_Create_R4Count;
BOOL m_Create_R5Count;
BOOL m_Create_R6Count;
BOOL m_Create_R7Count;
BOOL m_Create_R8Count;
BOOL m_Create_R9Count;
BOOL m_Create_ROtherCount;
BOOL m_Create_AllCover;
BOOL m_Create_AllFirstCover;
BOOL m_Create_AllCountCover;
BOOL m_Create_AllAboveMean;
BOOL m_Create_AllAboveMode;
BOOL m_Create_AllFirstAboveMean;
BOOL m_Create_AllFirstAboveMode;
BOOL m_Create_FirstAboveMeanCount;
BOOL m_Create_FirstAboveModeCount;
BOOL m_Create_AllAboveMeanCount;
BOOL m_Create_AllAboveModeCount;
BOOL m_Create_TotalFirstCount;
BOOL m_Create_TotalAllCount;

BOOL m_ApplyFuelModels;

BOOL m_DoTopoMetrics;
double m_TopoDistance;
double m_TopoLatitude;

BOOL m_EliminateOutliers;
double m_OutlierMin;
double m_OutlierMax;

CString m_csStatus;

CFileSpec m_DataFS;
double InterpolateGroundElevation(double X, double Y);

// this compaison function is used for sorting. It is located here because it uses global variables
int comparevalueforintensity(const void *arg1, const void *arg2)
{
	if (((PointRecord*)arg1)->CellNumber < ((PointRecord*)arg2)->CellNumber)
		return(-1);
	else if (((PointRecord*)arg1)->CellNumber > ((PointRecord*)arg2)->CellNumber)
		return(1);
	else {
		// compare elevation value to htmin
		if (((PointRecord*)arg1)->Elevation <= m_MinHeightForMetrics && ((PointRecord*)arg2)->Elevation <= m_MinHeightForMetrics)
			return(0);
		else if (((PointRecord*)arg1)->Elevation <= m_MinHeightForMetrics && ((PointRecord*)arg2)->Elevation > m_MinHeightForMetrics)
			return(1);
		else if (((PointRecord*)arg1)->Elevation > m_MinHeightForMetrics && ((PointRecord*)arg2)->Elevation <= m_MinHeightForMetrics)
			return(-1);
		else if (((PointRecord*)arg1)->Elevation > m_MinHeightForMetrics && ((PointRecord*)arg2)->Elevation > m_MinHeightForMetrics) {
			// compare intensity values
			if (((PointRecord*)arg1)->Value < ((PointRecord*)arg2)->Value)
				return(-1);
			else if (((PointRecord*)arg1)->Value > ((PointRecord*)arg2)->Value)
				return(1);
			else
				return(0);
		}
		else
			return(0);
	}
}

void usage()
{
	LTKCL_PrintProgramHeader();
	LTKCL_PrintProgramDescription();
	//			         1         2         3         4         5         6         7         8         9         10
	//			1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
	printf("\nSyntax: %s [switches] groundfile heightbreak cellsize outputfile\n", PROGRAM_NAME);
	printf("              datafile1 datafile2 ... datafile10\n");
	printf(" groundfile   Name for ground surface model (PLANS DTM with .dtm extension).\n");
	printf("              May be wildcard or text list file (extension .txt only)\n");
	printf(" heightbreak  Height break for cover calculation\n");
	printf(" cellsize     Desired grid cell size in the same units as LIDAR data\n");
	printf(" outputfile   Base name for output file. Metrics are stored in CSV format with\n");
	printf("              .csv extension unless the /nocsv switch is used. Other outputs\n");
	printf("              are stored in files named using the base name and additional\n");
	printf("              descriptive information.\n");
	printf(" datafile1    First LIDAR file (LDA, LAS, ASCII XYZ, ASCII XYZI formats)...\n");
	printf("              may be wildcard or text list file (extension .txt only)...omit\n");
	printf("              other datafile# parameters\n");
	printf(" datafile2    Second LIDAR file (LDA, LAS, ASCII XYZ, ASCII XYZI formats)\n");
	printf(" datafileN    Nth LIDAR file (LDA, LAS, ASCII XYZ, ASCII XYZI formats)\n");

	printf("\n Switches:\n");

	LTKCL_PrintStandardSwitchInfo();

	printf("  outlier:low,high Omit points with elevations below low and above high\n");
	printf("              when used with the /noground switch, low and high are\n");
	printf("              interpreted as elevations instead of heights above ground\n");
	printf("  class:string LAS files only: Specifies that only points with classification\n");
	printf("              values listed are to be included when computing metrics.\n");
	printf("              Classification values should be separated by a comma.\n");
	printf("              e.g. (2,3,4,5) and can range from 0 to 31.\n");
	printf("              If the first character in string is ~, the list is interpretted\n");
	printf("              as the classes you DO NOT want included in the subsample.\n");
	printf("              e.g. /class:~2,3 would include all class values EXCEPT 2 and 3.\n");
	printf("  ignoreoverlap Ignore points with the overlap flag set (LAS V1.4+ format)\n");
	printf("  id:identifier Include the identifier string as the last column in every\n");
	printf("              record in the outputfile. The identifier will be included in\n");
	printf("              all files containing metrics (elevation, intensity, and topo).\n");
	printf("              The identifier cannot include spaces.\n");
	//	printf("  ground:file Use the bare-earth surface model to normalize the LIDAR data\n");
	//	printf("  canopy:file Use the canopy surface model to normalize the LIDAR data\n");
	//	printf("  vegoffset:# Offset for canopy point determination\n");
	//	printf("  image       Create image file and corresponding world file\n");
	//	printf("  mediancut:# Cutoff value for median filter used to create red/blue image\n");
	//	printf("  invertcolor Invert the color codes for cells above/below median cutoff\n");
	printf("  minpts:#    Minimum number of points in a cell required to compute metrics\n");
	printf("              default is 4 points\n");
	printf("  minht:#     Minimum height for points used to compute metrics. Density always\n");
	printf("              uses all points including those with heights below the minimum.\n");
	printf("  nocsv       Do not create an output file for cell metrics\n");
	printf("  noground    Do not use a ground surface model. When this option is specified,\n");
	printf("              omit the groundfile parameter from the command line. Cover\n");
	printf("              estimates, densitytotal, densityabove, and densitycell metrics\n");
	printf("              are meaningless when no ground surface model is used unless the\n");
	printf("              LIDAR data have been normalized to the ground surface using some\n");
	printf("              other process.\n");
	printf("  diskground  Do not load ground surface models into memory. When this option\n");
	printf("              is specified, larger areas can be processed but processing will\n");
	printf("              be 4 to 5 times slower. Ignored when /noground option is used.\n");
	printf("  nointdtm    Do not create an internal ground model that corresponds to the\n");
	printf("              data extent. This option is most often used to prevent edge\n");
	printf("              artifacts when computing metrics for small areas using a\n");
	printf("              relatively larger cell size.\n");
	printf("  failnoz     Verify that all ground surface models can be opened and read\n");
	printf("              and exit immediately if there are any problems. The default\n");
	printf("              behavior is to go ahead and compute metrics using point\n");
	printf("              elevations (no normalization to create point heights).\n");
	//	printf("  alldensity  Use all returns when computing density (percent cover)\n");
	//	printf("              default is to use only first returns when computing density\n");
	printf("  first       Use only first returns to compute all metrics (default is to\n");
	printf("              use all returns for metrics)\n");
	//	printf("              /alldensity will be ignored if /first is used\n");
	//	printf("  intensity   Compute metrics using intensity values (default is elevation)\n");
	printf("  nointensity Do not compute metrics using intensity values (default is\n");
	printf("              to compute metrics using both intensity and elevation values)\n");
	printf("  rgb:color   Compute intensity metrics using the color value from the RGB\n");
	printf("              color for the returns. Valid with LAS version 1.2 and newer\n");
	printf("              data files that contain RGB information for each return (point\n");
	printf("              record types 2, 3 and 7). Valid color values are R, G, B, or N.\n");
	printf("              N (for NIR) is only valid for LAS 1.4+ files with point record\n");
	printf("              formats 8 and 10).\n");
	printf("  fuel        Apply fuel parameter models (cannot be used with /intensity,\n");
	printf("              /alldensity, or /first switches\n");
	printf("  grid:X,Y,W,H Force the origin of the output grid to be (X,Y) instead of\n");
	printf("              computing an origin from the data extents and force the grid to\n");
	printf("              be W units wide and H units high...W and H will be rounded up to\n");
	printf("              a multiple of cellsize\n");
	printf("  gridxy:X1,Y1,X2,Y2 Force the origin of the output grid to be (X1,Y1) instead\n");
	printf("              of computing an origin from the data extents and force the grid\n");
	printf("              to use (X2,Y2) as the upper right corner of the coverage area.\n");
	printf("              The actual upper right corner will be adjusted to be a multiple\n");
	printf("              of cellsize\n");
	printf("  align:filename Force the origin and extent of the output grid to match the\n");
	printf("              lower left corner and extent of the specified PLANS format DTM\n");
	printf("              file\n");
	printf("  extent:filename Force the origin and extent of the output grid to match the\n");
	printf("              lower left corner and extent of the specified PLANS format DTM\n");
	printf("              file but adjust the origin to be an even multiple of the cell\n");
	printf("              size and the width and height to be multiples of the cell size.\n");
	printf("  buffer:width Add a buffer to the data extent specified by /grid or /gridxy\n");
	printf("              when computing metrics but only output data for the specified\n");
	printf("              extent. The buffer width is first converted to a cellbuffer and\n");
	printf("              then added all around the extent. The actual buffer width may be\n");
	printf("              slightly larger than specified by width.\n");
	printf("  cellbuffer:# Add a buffer to the data extent specified by /grid or /gridxy\n");
	printf("              when computing metrics but only output data for the specified\n");
	printf("              extent. The buffer (number of cells) is added around the extent.\n");
	printf("  strata:[#,#,...] Count returns in various height strata. # gives the upper\n");
	printf("              limit for each strata. Returns are counted if their height\n");
	printf("              is >= the lower limit and < the upper limit. The first strata\n");
	printf("              contains points < the first limit. The last strata contains\n");
	printf("              points >= the last limit. Default strata: 0.15, 1.37, 5, 10,\n");
	printf("              20, 30.\n");
	printf("  intstrata:[#,#,...] Compute metrics using the intensity values in various\n");
	printf("              height strata. Strata for intensity metrics are defined in\n");
	printf("              the same way as the /strata option. Default strata: 0.25, 1.37.\n");
	printf("  kde:[window,mult] Compute the number of modes and minimum and maximum node\n");
	printf("              using a kernal density estimator. Window is the width of a\n");
	printf("              moving average smoothing window in data units and mult is a\n");
	printf("              multiplier for the bandwidth parameter of the KDE. Default\n");
	printf("              window is 2.5 and the multiplier is 1.0\n");
	printf("  ascii       Store raster files in ASCII raster format for direct import\n");
	printf("              into ArcGIS. Using this option preserves metrics with negative\n");
	printf("              values. Such values are lost when raster data are saved in PLANS\n");
	printf("              DTM format. This switch is ignored unless used with /raster\n");
	printf("  topo:dist,lat Compute topographic metrics using the groundfile(s) and output\n");
	printf("              them in a separate file. Distance is the cell size for the 3 by\n");
	printf("              3 cell analysis area and lat is the latitude (+north, -south).\n");
	printf("  raster:layers Create raster files containing the metrics. layers is a list\n");
	printf("              of metric names separated by commas. Raster files are stored\n");
	printf("              in PLANS DTM format unless the /ascii switch is included.\n");
	printf("\n              Available metrics are:\n");
	printf("                 count     number of returns above min ht\n");
	printf("                 densitytotal total returns used for calculating cover\n");		// total 1st returns
	printf("                 densityabove returns above heightbreak\n");			// 1st returns above cover ht
	printf("                 densitycell  density of returns used for calculating cover\n");		// density all first returns (pulses)
	printf("                 min       minimum value for cell\n");
	printf("                 max       maximum value for cell\n");
	printf("                 mean      mean value for cell\n");
	printf("                 mode      mode for cell\n");
	printf("                 stddev    standard deviation of cell values\n");
	printf("                 variance  variance of cell values\n");
	printf("                 cv        coefficient of variation for cell\n");
	printf("                 cover     cover estimate for cell\n");									// 1st returns above cover ht / total first returns
	printf("                 abovemean proportion of 1st returns above the mean\n");				// 1st returns above mean / total first returns
	printf("                 abovemode proportion of 1st returns above the mode\n");				// 1st returns above mode / total first returns
	printf("                 skewness  skewness computed for cell\n");
	printf("                 kurtosis  kurtosis computed for cell\n");
	printf("                 aad       average absolute deviation from mean computed for\n");
	printf("                           cell\n");
	printf("                 p01       1st percentile value for cell (must be p01, not p1)\n");
	printf("                 p05       5th percentile value for cell (must be p05, not p5)\n");
	printf("                 p10       10th percentile value for cell\n");
	printf("                 p20       20th percentile value for cell\n");
	printf("                 p25       25th percentile value for cell\n");
	printf("                 p30       30th percentile value for cell\n");
	printf("                 p40       40th percentile value for cell\n");
	printf("                 p50       50th percentile value (median) for cell\n");
	printf("                 p60       60th percentile value for cell\n");
	printf("                 p70       70th percentile value for cell\n");
	printf("                 p75       75th percentile value for cell\n");
	printf("                 p80       80th percentile value for cell\n");
	printf("                 p90       90th percentile value for cell\n");
	printf("                 p95       95th percentile value for cell\n");
	printf("                 p99       99th percentile value for cell\n");
	printf("                 iq        75th percentile minus 25th percentile for cell\n");
	printf("                 90m10     90th percentile minus 10th percentile for cell\n");
	printf("                 95m05     95th percentile minus 5th percentile for cell\n");

	printf("                 r1count   count of return 1 points above min ht\n");
	printf("                 r2count   count of return 2 points above min ht\n");
	printf("                 r3count   count of return 3 points above min ht\n");
	printf("                 r4count   count of return 4 points above min ht\n");
	printf("                 r5count   count of return 5 points above min ht\n");
	printf("                 r6count   count of return 6 points above min ht\n");
	printf("                 r7count   count of return 7 points above min ht\n");
	printf("                 r8count   count of return 8 points above min ht\n");
	printf("                 r9count   count of return 9 points above min ht\n");
	printf("                 rothercount count of other returns above min ht\n");
	printf("                 allcover  (all returns above cover ht) / (total returns)\n");
	printf("                 afcover   (all returns above cover ht) / (total first returns)\n");
	printf("                 allcount  number of returns above cover ht\n");
	printf("                 allabovemean (all returns above mean ht) / (total returns)\n");
	printf("                 allabovemode (all returns above ht mode) / (total returns)\n");
	printf("                 afabovemean (all returns above mean ht) / (total 1st returns)\n");
	printf("                 afabovemode (all returns above ht mode) / (total 1st returns)\n");
	printf("                 fcountmean number of first returns above mean ht\n");
	printf("                 fcountmode number of first returns above ht mode\n");
	printf("                 allcountmean number of returns above mean ht\n");
	printf("                 allcountmode number of returns above ht mode\n");
	printf("                 totalfirst total number of 1st returns\n");
	printf("                 totalall  total number of returns\n");

	printf("              An example would be /raster:min,max,p75 to produce raster files\n");
	printf("              containing the minimum, maximum and 75th percentile values for\n");
	printf("              each cell\n");

	//			         1         2         3         4         5         6         7         8         9         10
	//			1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890

	//	LTKCL_LaunchHelpPage();
}

void InitializeGlobalVariables()
{
	m_StatParameter = INTENSITY_VALUE;
	m_UseFirstReturnsForDensity = TRUE;
	m_UseFirstReturnsForAllMetrics = FALSE;

	m_Verbose = FALSE;
	m_CreateOutlierImage = FALSE;

	m_TestTerrain = FALSE;

	m_NoCSVFile = FALSE;

	m_UseGround = TRUE;
	m_GroundFileCount = 0;
	//	m_GroundDTM = NULL;
	m_GroundFromDisk = FALSE;

	m_UserGrid = FALSE;
	m_UserGridXY = FALSE;
	m_ForceAlignment = FALSE;
	m_ForceExtent = FALSE;

	m_MinHeightForMetrics = -99999.0;

	m_DoKDEStats = FALSE;

	m_NoItensityMetrics = FALSE;

	m_DoRGBMetrics = FALSE;
	m_DoNIRMetrics = FALSE;

	m_CreateInternalDTM = TRUE;

	m_SkipOverlapPoints = FALSE;

	m_DoHeightStrata = FALSE;
	m_DoHeightStrataIntensity = FALSE;
	m_HeightStrataCount = 0;
	m_HeightStrataIntensityCount = 0;

	m_ComputePA = TRUE;			// always true to compute profile area

	m_AddBufferDistance = FALSE;
	m_AddBufferCells = FALSE;
	m_BufferDistance = 0.0;
	m_BufferCells = 0;

	m_UseCanopy = FALSE;
	m_CanopyFileName = _T("");

	m_RasterFiles = FALSE;
	m_SaveASCIIRaster = FALSE;
	m_Create_PtCountGrid = FALSE;
	m_Create_PtCountTotalGrid = FALSE;
	m_Create_PtCountAboveGrid = FALSE;
	m_Create_MinGrid = FALSE;
	m_Create_MaxGrid = FALSE;
	m_Create_MeanGrid = FALSE;
	m_Create_ModeGrid = FALSE;
	m_Create_StdDevGrid = FALSE;
	m_Create_VarianceGrid = FALSE;
	m_Create_CVGrid = FALSE;
	m_Create_SkewnessGrid = FALSE;
	m_Create_KurtosisGrid = FALSE;
	m_Create_AADGrid = FALSE;
	m_Create_P01Grid = FALSE;
	m_Create_P05Grid = FALSE;
	m_Create_P10Grid = FALSE;
	m_Create_P20Grid = FALSE;
	m_Create_P25Grid = FALSE;
	m_Create_P30Grid = FALSE;
	m_Create_P40Grid = FALSE;
	m_Create_P50Grid = FALSE;
	m_Create_P60Grid = FALSE;
	m_Create_P70Grid = FALSE;
	m_Create_P75Grid = FALSE;
	m_Create_P80Grid = FALSE;
	m_Create_P90Grid = FALSE;
	m_Create_P95Grid = FALSE;
	m_Create_P99Grid = FALSE;
	m_Create_IQGrid = FALSE;
	m_Create_90m10Grid = FALSE;
	m_Create_95m05Grid = FALSE;
	m_Create_DensityGrid = FALSE;
	m_Create_DensityMeanGrid = FALSE;
	m_Create_DensityModeGrid = FALSE;
	m_Create_DensityCell = FALSE;

	m_Create_R1Count = FALSE;
	m_Create_R2Count = FALSE;
	m_Create_R3Count = FALSE;
	m_Create_R4Count = FALSE;
	m_Create_R5Count = FALSE;
	m_Create_R6Count = FALSE;
	m_Create_R7Count = FALSE;
	m_Create_R8Count = FALSE;
	m_Create_R9Count = FALSE;
	m_Create_ROtherCount = FALSE;
	m_Create_AllCover = FALSE;
	m_Create_AllFirstCover = FALSE;
	m_Create_AllCountCover = FALSE;
	m_Create_AllAboveMean = FALSE;
	m_Create_AllAboveMode = FALSE;
	m_Create_AllFirstAboveMean = FALSE;
	m_Create_AllFirstAboveMode = FALSE;
	m_Create_FirstAboveMeanCount = FALSE;
	m_Create_FirstAboveModeCount = FALSE;
	m_Create_AllAboveMeanCount = FALSE;
	m_Create_AllAboveModeCount = FALSE;
	m_Create_TotalFirstCount = FALSE;
	m_Create_TotalAllCount = FALSE;

	m_ApplyFuelModels = FALSE;

	m_UseGlobalIdentifier = FALSE;
	m_GlobalIdentifier.Empty();

	m_DoTopoMetrics = FALSE;

	m_IncludeSpecificClasses = FALSE;
	m_NumberOfClassCodes = 0;

	for (int i = 0; i < NUMBER_OF_CLASS_CODES; i++)
		m_ClassCodes[i] = 0;
}

int PresentInteractiveDialog()
{
	if (m_RunInteractive) {
		usage();
		::MessageBox(NULL, "Interactive mode is not currently implemented", PROGRAM_NAME, MB_OK);
		return(1);		// temp...needs to reflect success/failure in interactive mode or user pressing cancel
	}
	return(0);
}

int ParseCommandLine()
{
	int nRetCode = 0;
	int i;
	CString csTemp;

	if (m_clp.CheckHelp() || m_clp.ParamCount() - m_clp.SwitchCount() < MINIMUM_CL_PARAMS) {
		usage();
		return(1);
	}

	// get switches
	m_Verbose = m_clp.Switch("verbose");
	if (m_clp.Switch("noground"))			// turn off ground model
		m_UseGround = FALSE;
	//	m_GroundFileName = _T("");
	//	if (m_UseGround)
	//		m_GroundFileName = m_clp.GetSwitchStr("ground", "");

	m_IncludeSpecificClasses = m_clp.Switch("class");

	m_SkipOverlapPoints = m_clp.Switch("ignoreoverlap");

	m_GroundFromDisk = m_clp.Switch("diskground");

	m_TestTerrain = m_clp.Switch("failnoz");

	if (m_clp.Switch("nointdtm"))
		m_CreateInternalDTM = FALSE;

	m_NoCSVFile = m_clp.Switch("nocsv");

	m_ApplyFuelModels = m_clp.Switch("fuel");

	// get global identifier
	m_UseGlobalIdentifier = m_clp.Switch("id");
	if (m_UseGlobalIdentifier) {
		m_GlobalIdentifier = m_clp.GetSwitchStr("id", "");
		if (m_GlobalIdentifier.IsEmpty()) {
			m_UseGlobalIdentifier = FALSE;
			LTKCL_PrintStatus("Error: Invalid option for /id switch");

			nRetCode = 1;
		}
	}

	// get the string indicating which LAS classification codes should be included in the sample
	if (m_IncludeSpecificClasses) {
		CString csTemp;
		csTemp = m_clp.GetSwitchStr("class", "");

		// parse the classification codes
		if (!csTemp.IsEmpty()) {
			// read the codes and set flags
			BOOL omitclass = FALSE;
			char classcodes[1024];
			strcpy(classcodes, csTemp);

			// 11/26/2014 possible change to /class option to allow you to specify the classes you don't want
			// look for "~" in classcodes[0] to reverse interpretation of which classes to use
			// if found, turn on all m_ClassCodes and remmove the first character...should be able to change first character to a space...if not, start strtok() on second character

			// see if we are omitting classes instead of including class..."~" in first character
			if (classcodes[0] == '~') {
				omitclass = TRUE;

				// turn on all classes
				for (i = 0; i < NUMBER_OF_CLASS_CODES; i++)
					m_ClassCodes[i] = 1;

				// replace first character
				classcodes[0] = ' ';
			}

			char* cls = strtok(classcodes, " ,");
			int clscnt = 0;
			int tcls;
			while (cls) {
				tcls = atoi(cls);
				if (tcls >= 0 && tcls < NUMBER_OF_CLASS_CODES) {
					if (omitclass)
						m_ClassCodes[tcls] = 0;
					else
						m_ClassCodes[tcls] = 1;
				}
				else {
					// bad class code
					csTemp.Format("LAS class code: %i out of range...must be 0 - %i", tcls, NUMBER_OF_CLASS_CODES - 1);
					LTKCL_PrintStatus(csTemp);
				}

				clscnt++;
				cls = strtok(NULL, " ,");
			}

			m_NumberOfClassCodes = clscnt;
		}
		if (m_NumberOfClassCodes == 0) {
			m_IncludeSpecificClasses = FALSE;
			LTKCL_PrintStatus("Error: Invalid options for /class switch...no valid classification codes");

			nRetCode = 1;
		}
	}

	m_DoKDEStats = m_clp.Switch("kde");

	if (m_DoKDEStats) {
		CString temp = m_clp.GetSwitchStr("kde", "");
		if (!temp.IsEmpty()) {
			sscanf(temp, "%lf,%lf", &m_KDEWindowSize, &m_KDEBandwidthMultiplier);
		}
		else {
			// default
			m_KDEBandwidthMultiplier = 1.0;
			m_KDEWindowSize = 2.5;
		}
	}

	// check for height strata options
	m_DoHeightStrata = m_clp.Switch("strata");
	if (m_DoHeightStrata) {
		CString temp = m_clp.GetSwitchStr("strata", "");
		if (!temp.IsEmpty()) {
			m_HeightStrataCount = 0;
			char* c = strtok(temp.LockBuffer(), ",");
			while (c) {
				m_HeightStrata[m_HeightStrataCount] = atof(c);
				m_HeightStrataCount++;

				// check number of strata
				if (m_HeightStrataCount >= MAX_NUMBER_OF_STRATA) {
					LTKCL_PrintStatus("Too many strata for /strata...maximum is 31");
					return(1);

					// need break if we add a return variable
//					break;
				}
				c = strtok(NULL, ",");
			}
			temp.ReleaseBuffer();

			// add a final strata to define upper bound
			m_HeightStrata[m_HeightStrataCount] = DBL_MAX;
			m_HeightStrataCount++;
		}
		else {
			// default
			m_HeightStrataCount = 7;
			m_HeightStrata[0] = 0.15;
			m_HeightStrata[1] = 1.37;
			m_HeightStrata[2] = 5.0;
			m_HeightStrata[3] = 10.0;
			m_HeightStrata[4] = 20.0;
			m_HeightStrata[5] = 30.0;
			m_HeightStrata[6] = DBL_MAX;

		}
	}

	m_DoHeightStrataIntensity = m_clp.Switch("intstrata");
	if (m_DoHeightStrataIntensity) {
		CString temp = m_clp.GetSwitchStr("intstrata", "");
		if (!temp.IsEmpty()) {
			m_HeightStrataIntensityCount = 0;
			char* c = strtok(temp.LockBuffer(), ",");
			while (c) {
				m_HeightStrataIntensity[m_HeightStrataIntensityCount] = atof(c);
				m_HeightStrataIntensityCount++;

				if (m_HeightStrataIntensityCount >= MAX_NUMBER_OF_STRATA) {
					LTKCL_PrintStatus("Too many strata for /intstrata...maximum is 31");
					return(1);

					// need break if we add a return variable
//					break;
				}
				c = strtok(NULL, ",");
			}
			temp.ReleaseBuffer();

			// add a final strata to define upper bound
			m_HeightStrataIntensity[m_HeightStrataIntensityCount] = DBL_MAX;
			m_HeightStrataIntensityCount++;
		}
		else {
			// default
			m_HeightStrataIntensityCount = 3;
			m_HeightStrataIntensity[0] = 0.15;
			m_HeightStrataIntensity[1] = 1.37;
			m_HeightStrataIntensity[2] = DBL_MAX;
		}
	}

	m_DoTopoMetrics = m_clp.Switch("topo");
	if (m_DoTopoMetrics) {
		CString csParam = m_clp.GetSwitchStr("topo", "100.0,45.0");
		sscanf(csParam, "%lf,%lf", &m_TopoDistance, &m_TopoLatitude);
	}

	m_UserGrid = m_clp.Switch("grid");
	if (m_UserGrid) {
		CString csOrigin = m_clp.GetSwitchStr("grid", "-9999.0,-9999.0,-9999.0,-9999.0");
		sscanf(csOrigin, "%lf,%lf,%lf,%lf", &m_CustomOriginX, &m_CustomOriginY, &m_CustomGridWidth, &m_CustomGridHeight);
		if (m_CustomOriginX == -9999.0 || m_CustomOriginY == -9999.0) {
			csTemp.Format("Invalid grid switch parameters: (%s)", (LPCSTR) csOrigin);
			LTKCL_PrintStatus(csTemp);
			m_UserGrid = FALSE;
			nRetCode = 5;
		}
	}

	m_UserGridXY = m_clp.Switch("gridxy");
	if (m_UserGridXY) {
		CString csOrigin = m_clp.GetSwitchStr("gridxy", "-9999.0,-9999.0,-9999.0,-9999.0");
		sscanf(csOrigin, "%lf,%lf,%lf,%lf", &m_CustomOriginX, &m_CustomOriginY, &m_CustomMaxX, &m_CustomMaxY);
		if (m_CustomOriginX == -9999.0 || m_CustomOriginY == -9999.0) {
			csTemp.Format("Invalid gridxy switch parameters: (%s)", (LPCSTR) csOrigin);
			LTKCL_PrintStatus(csTemp);
			m_UserGridXY = FALSE;
			nRetCode = 6;
		}
	}

	m_ForceAlignment = m_clp.Switch("align");
	m_ForceExtent = m_clp.Switch("extent");
	if (m_ForceAlignment && m_ForceExtent) {
		csTemp.Format("You cannot use the /align and /extent switches at the same time");
		LTKCL_PrintStatus(csTemp);
		m_ForceAlignment = FALSE;
		m_ForceExtent = FALSE;
		nRetCode = 12;
	}
	else if (m_ForceAlignment || m_ForceExtent) {
		// read model name, if valid read header to set output origin and extent
		CString AlignmentModel;
		if (m_ForceAlignment)
			AlignmentModel = m_clp.GetSwitchStr("align", "");
		if (m_ForceExtent)
			AlignmentModel = m_clp.GetSwitchStr("extent", "");

		if (!AlignmentModel.IsEmpty()) {
			PlansDTM tdtm(AlignmentModel, FALSE, FALSE);
			if (tdtm.IsValid()) {
				m_CustomOriginX = tdtm.OriginX();
				m_CustomOriginY = tdtm.OriginY();
				m_CustomMaxX = tdtm.OriginX() + tdtm.Width();
				m_CustomMaxY = tdtm.OriginY() + tdtm.Height();
			}
			else {
				csTemp.Format("Could not open the .dtm file specified for alignment: %s", (LPCSTR) AlignmentModel);
				LTKCL_PrintStatus(csTemp);
				m_ForceAlignment = FALSE;
				m_ForceExtent = FALSE;
				nRetCode = 8;
			}
		}
		else {
			LTKCL_PrintStatus("Model for /align or /extent switch not specified");
			m_ForceAlignment = FALSE;
			m_ForceExtent = FALSE;
			nRetCode = 7;
		}
	}

	// buffer related switches
	m_AddBufferDistance = m_clp.Switch("buffer");
	if (m_AddBufferDistance) {
		CString BufferDist = m_clp.GetSwitchStr("buffer", "0.0");
		if (!BufferDist.IsEmpty()) {
			m_BufferDistance = atof(BufferDist);
			if (m_BufferDistance == 0.0)
				m_AddBufferDistance = FALSE;
		}
	}


	m_AddBufferCells = m_clp.Switch("cellbuffer");
	if (m_AddBufferCells) {
		CString BufferDist = m_clp.GetSwitchStr("cellbuffer", "0");
		if (!BufferDist.IsEmpty()) {
			m_BufferCells = atoi(BufferDist);
			if (m_BufferCells == 0)
				m_AddBufferCells = FALSE;
		}
	}

	m_UseCanopy = m_clp.Switch("canopy");
	m_CanopyFileName = _T("");
	if (m_UseCanopy)
		m_CanopyFileName = m_clp.GetSwitchStr("canopy", "");

	// always use first returns for density calculation unless allreturns switch is used
//	if (m_clp.Switch("alldensity"))
//		m_UseFirstReturnsForDensity = FALSE;

	if (m_clp.Switch("first")) {
		m_UseFirstReturnsForAllMetrics = TRUE;
		m_UseFirstReturnsForDensity = TRUE;
	}

	//	if (m_clp.Switch("intensity"))
	//		m_StatParameter = INTENSITY_VALUE;

	m_NoItensityMetrics = m_clp.Switch("nointensity");
	if (m_NoItensityMetrics)
		m_StatParameter = ELEVATION_VALUE;

	m_DoRGBMetrics = m_clp.Switch("rgb");
	if (m_DoRGBMetrics) {
		// get the color value R, G, or B (no case)
		CString temp = m_clp.GetSwitchStr("rgb", "");
		if (temp.IsEmpty()) {
			m_DoRGBMetrics = FALSE;
			LTKCL_PrintStatus("No color specified for /RGB switch");
			nRetCode = 19;
		}
		else {
			if (temp.CompareNoCase("r") == 0)
				m_RGBColor = RGB_RED;
			else if (temp.CompareNoCase("g") == 0)
				m_RGBColor = RGB_GREEN;
			else if (temp.CompareNoCase("b") == 0)
				m_RGBColor = RGB_BLUE;
			else if (temp.CompareNoCase("n") == 0) {
				m_DoNIRMetrics = TRUE;
				m_RGBColor = RGB_NIR;
			}
			else {
				csTemp.Format("Invalid color option for /RGB switch: %s", (LPCSTR) temp);
				LTKCL_PrintStatus(csTemp);
				nRetCode = 18;
			}
		}
	}

	m_CanopyHeightOffset = atof(m_clp.GetSwitchStr("vegoffset", "0.0"));

	m_MinCellPoints = m_clp.GetSwitchInt("minpts", 3);

	m_MinHeightForMetrics = atof(m_clp.GetSwitchStr("minht", "-99999.0"));
	if (m_MinHeightForMetrics == -99999.0)
		m_MinHeightForMetrics = atof(m_clp.GetSwitchStr("htmin", "-99999.0"));

	// get outlier parameters
	m_EliminateOutliers = m_clp.Switch("outlier");
	if (m_EliminateOutliers) {
		CString temp = m_clp.GetSwitchStr("outlier", "");
		if (!temp.IsEmpty()) {
			sscanf(temp, "%lf,%lf", &m_OutlierMin, &m_OutlierMax);
		}
	}

	// check for ASCII raster format
	m_SaveASCIIRaster = m_clp.Switch("ascii");

	// check for raster files
	m_RasterFiles = m_clp.Switch("raster");
	if (m_RasterFiles) {
		// get options
		CString temp = m_clp.GetSwitchStr("raster", "");
		if (!temp.IsEmpty()) {
			temp.MakeLower();
			if (temp.Find("count") >= 0)
				m_Create_PtCountGrid = TRUE;
			if (temp.Find("densitytotal") >= 0)
				m_Create_PtCountTotalGrid = TRUE;
			if (temp.Find("densitycell") >= 0)
				m_Create_DensityCell = TRUE;
			if (temp.Find("densityabove") >= 0)
				m_Create_PtCountAboveGrid = TRUE;
			if (temp.Find("min") >= 0)
				m_Create_MinGrid = TRUE;
			if (temp.Find("max") >= 0)
				m_Create_MaxGrid = TRUE;
			if (temp.Find("mean") >= 0)
				m_Create_MeanGrid = TRUE;
			if (temp.Find("mode") >= 0)
				m_Create_ModeGrid = TRUE;
			if (temp.Find("stddev") >= 0)
				m_Create_StdDevGrid = TRUE;
			if (temp.Find("variance") >= 0)
				m_Create_VarianceGrid = TRUE;
			if (temp.Find("cv") >= 0)
				m_Create_CVGrid = TRUE;
			if (temp.Find("p01") >= 0)
				m_Create_P01Grid = TRUE;
			if (temp.Find("p05") >= 0)
				m_Create_P05Grid = TRUE;
			if (temp.Find("p10") >= 0)
				m_Create_P10Grid = TRUE;
			if (temp.Find("p20") >= 0)
				m_Create_P20Grid = TRUE;
			if (temp.Find("p25") >= 0)
				m_Create_P25Grid = TRUE;
			if (temp.Find("p30") >= 0)
				m_Create_P30Grid = TRUE;
			if (temp.Find("p40") >= 0)
				m_Create_P40Grid = TRUE;
			if (temp.Find("p50") >= 0)
				m_Create_P50Grid = TRUE;
			if (temp.Find("p60") >= 0)
				m_Create_P60Grid = TRUE;
			if (temp.Find("p70") >= 0)
				m_Create_P70Grid = TRUE;
			if (temp.Find("p75") >= 0)
				m_Create_P75Grid = TRUE;
			if (temp.Find("p80") >= 0)
				m_Create_P80Grid = TRUE;
			if (temp.Find("p90") >= 0)
				m_Create_P90Grid = TRUE;
			if (temp.Find("p95") >= 0)
				m_Create_P95Grid = TRUE;
			if (temp.Find("p99") >= 0)
				m_Create_P99Grid = TRUE;
			if (temp.Find("iq") >= 0)
				m_Create_IQGrid = TRUE;
			if (temp.Find("skewness") >= 0)
				m_Create_SkewnessGrid = TRUE;
			if (temp.Find("kurtosis") >= 0)
				m_Create_KurtosisGrid = TRUE;
			if (temp.Find("aad") >= 0)
				m_Create_AADGrid = TRUE;
			if (temp.Find("90m10") >= 0)
				m_Create_90m10Grid = TRUE;
			if (temp.Find("95m05") >= 0)
				m_Create_95m05Grid = TRUE;
			if (temp.Find("cover") >= 0)
				m_Create_DensityGrid = TRUE;
			if (temp.Find("abovemean") >= 0)
				m_Create_DensityMeanGrid = TRUE;
			if (temp.Find("abovemoder") >= 0)
				m_Create_DensityModeGrid = TRUE;

			if (temp.Find("r1count") >= 0)
				m_Create_R1Count = TRUE;
			if (temp.Find("r2count") >= 0)
				m_Create_R2Count = TRUE;
			if (temp.Find("r3count") >= 0)
				m_Create_R3Count = TRUE;
			if (temp.Find("r4count") >= 0)
				m_Create_R4Count = TRUE;
			if (temp.Find("r5count") >= 0)
				m_Create_R5Count = TRUE;
			if (temp.Find("r6count") >= 0)
				m_Create_R6Count = TRUE;
			if (temp.Find("r7count") >= 0)
				m_Create_R7Count = TRUE;
			if (temp.Find("r8count") >= 0)
				m_Create_R8Count = TRUE;
			if (temp.Find("r9count") >= 0)
				m_Create_R9Count = TRUE;
			if (temp.Find("rothercount") >= 0)
				m_Create_ROtherCount = TRUE;
			if (temp.Find("allcover") >= 0)
				m_Create_AllCover = TRUE;
			if (temp.Find("afcover") >= 0)
				m_Create_AllFirstCover = TRUE;
			if (temp.Find("allcount") >= 0)
				m_Create_AllCountCover = TRUE;
			if (temp.Find("allabovemean") >= 0)
				m_Create_AllAboveMean = TRUE;
			if (temp.Find("allabovemode") >= 0)
				m_Create_AllAboveMode = TRUE;
			if (temp.Find("afabovemean") >= 0)
				m_Create_AllFirstAboveMean = TRUE;
			if (temp.Find("afabovemode") >= 0)
				m_Create_AllFirstAboveMode = TRUE;
			if (temp.Find("fcountmean") >= 0)
				m_Create_FirstAboveMeanCount = TRUE;
			if (temp.Find("fcountmode") >= 0)
				m_Create_FirstAboveModeCount = TRUE;
			if (temp.Find("allcountmean") >= 0)
				m_Create_AllAboveMeanCount = TRUE;
			if (temp.Find("allcountmode") >= 0)
				m_Create_AllAboveModeCount = TRUE;
			if (temp.Find("totalfirst") >= 0)
				m_Create_TotalFirstCount = TRUE;
			if (temp.Find("totalall") >= 0)
				m_Create_TotalAllCount = TRUE;
		}
	}

	// parse arguments from command line
	m_GroundFileName = _T("");
	if (m_UseGround) {
		m_GroundFileName = m_clp.ParamStr(m_clp.FirstNonSwitchIndex());
		m_HeightBreak = atof(m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 1));
		m_CellSize = atof(m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 2));
		m_OutputFile = m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 3);
		m_FirstDataFile = m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 4);
		m_GroundFileCount = 1;
	}
	else {
		m_GroundFileName = _T("");
		m_HeightBreak = atof(m_clp.ParamStr(m_clp.FirstNonSwitchIndex()));
		m_CellSize = atof(m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 1));
		m_OutputFile = m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 2);
		m_FirstDataFile = m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 3);
		m_GroundFileCount = 0;
	}
	m_DataFileCount = 1;

	// see if list file was given on command line
	m_DataFS.SetFullSpec(m_FirstDataFile);
	if (m_DataFS.Extension().CompareNoCase(".txt") == 0) {
		if (m_DataFS.Exists()) {
			// read data files from list file
			CDataFile lst(m_DataFS.GetFullSpec());
			if (lst.IsValid()) {
				m_DataFileCount = 0;
				char buf[1024];
				CFileSpec TempFS;
				while (lst.NewReadASCIILine(buf)) {
					TempFS.SetFullSpec(buf);
					if (TempFS.Exists()) {
						m_DataFile.Add(TempFS.GetFullSpec());
						m_DataFileCount++;
					}
				}
			}
		}
		else {
			csTemp.Format("Can't open list file (doesn't exist): %s", (LPCSTR) m_FirstDataFile);
			LTKCL_PrintStatus(csTemp);
			nRetCode = 9;
			m_DataFileCount = 0;
		}
	}
	else {
		// see if first data file expands to multiple files
		CFileFind finder;
		int FileCount = 0;
		BOOL bWorking = finder.FindFile(m_FirstDataFile);
		while (bWorking) {
			bWorking = finder.FindNextFile();
			FileCount++;
		}

		if (FileCount > 1 || (FileCount == 1 && m_FirstDataFile.FindOneOf("*?") >= 0)) {
			bWorking = finder.FindFile(m_FirstDataFile);
			m_DataFileCount = 0;
			while (bWorking) {
				bWorking = finder.FindNextFile();
				m_DataFile.Add(finder.GetFilePath());
				m_DataFileCount++;
			}
		}
		else {
			m_DataFile.Add(m_FirstDataFile);
			if (m_UseGround) {
				for (i = m_clp.FirstNonSwitchIndex() + 5; i < m_clp.ParamCount(); i++) {
					m_DataFile.Add(CString(m_clp.ParamStr(i)));
					m_DataFileCount++;
				}
			}
			else {
				for (i = m_clp.FirstNonSwitchIndex() + 4; i < m_clp.ParamCount(); i++) {
					m_DataFile.Add(CString(m_clp.ParamStr(i)));
					m_DataFileCount++;
				}
			}
		}
	}

	// see if ground file given on command line expands into more than 1 file
/*	if (m_UseGround) {
		m_DataFS.SetFullSpec(m_GroundFileName);
		if (m_DataFS.Extension().CompareNoCase(".txt") == 0) {		// ground file is list
			if (m_DataFS.Exists()) {
				// read ground files from list file
				CDataFile lst(m_DataFS.GetFullSpec());
				if (lst.IsValid()) {
					m_GroundFileCount = 0;
					char buf[1024];
					CFileSpec TempFS;
					while (lst.NewReadASCIILine(buf)) {
						TempFS.SetFullSpec(buf);
						if (TempFS.Exists()) {
							m_GroundFiles.Add(TempFS.GetFullSpec());
							m_GroundFileCount ++;
						}
					}
				}
			}
			else {
				csTemp.Format("Can't open ground model list file (doesn't exist): %s", m_GroundFileName);
				LTKCL_PrintStatus(csTemp);
				nRetCode = 10;
				m_GroundFileCount = 0;
			}
		}
		else {
			// see if ground file expands to multiple files
			CFileFind finder;
			int FileCount = 0;
			BOOL bWorking = finder.FindFile(m_GroundFileName);
			while (bWorking) {
				bWorking = finder.FindNextFile();
				FileCount ++;
			}

			if (FileCount > 1 || (FileCount == 1 && m_GroundFileName.FindOneOf("*?") >= 0)) {
				bWorking = finder.FindFile(m_GroundFileName);
				m_GroundFileCount = 0;
				while (bWorking) {
					bWorking = finder.FindNextFile();
					m_GroundFiles.Add(finder.GetFilePath());
					m_GroundFileCount ++;
				}
			}
			else {		// single file given on command line
				m_GroundFiles.Add(m_GroundFileName);
				m_GroundFileCount = 1;
			}
		}
	}
*/

// if using ground files, open them and validate the models
// if using a ground file, open it and validate the model
	if (m_UseGround) {
		m_Terrain.SetFileSpec(m_GroundFileName);
		if (m_Terrain.IsValid())
			m_GroundFileCount = m_Terrain.GetModelCount();
		else
			m_GroundFileCount = 0;
	}

	/*	if (m_UseGround) {
			// allocate memory for DTM files
			m_GroundDTM = new PlansDTM[m_GroundFileCount];
			if (m_GroundDTM) {
				// commented logic will force the model to be loaded into memory when there is only one model
				// this will happen later in the 1/8/2009 revisions to use multiple models
	//			if (m_GroundFileCount == 1) {
	//				m_GroundDTM[0].OpenAndRead(m_GroundFiles[0]);
	//			}
	//			else {
					// read all model headers
					for (i = 0; i < m_GroundFileCount; i ++) {
						m_GroundDTM[i].LoadElevations(m_GroundFiles[i]);		// patch access
					}
	//			}
			}
			else {
				csTemp.Format("Can't allocate memory for ground model list");
				LTKCL_PrintStatus(csTemp);
				nRetCode = 11;
				m_GroundFileCount = 0;
			}
		}
	*/
	if (m_UseCanopy) {
		m_CanopyDTM.OpenAndRead(m_CanopyFileName);
	}

	//	// make sure we aren't trying to compute fuel variables with intensity
	//	if (m_StatParameter == INTENSITY_VALUE && m_ApplyFuelModels) {
	//		printf("Cannot compute fuel parameters using intensity values\n");
	//		printf("/fuel cannot be used with /intensity\n");
	//		return(1);
	//	}

	//	// make sure we aren't trying to compute fuel variables using all returns for density
	//	if (!m_UseFirstReturnsForDensity && m_ApplyFuelModels) {
	//		printf("Cannot compute fuel parameters using all returns for density calculations\n");
	//		printf("/fuel cannot be used with /alldensity\n");
	//		return(1);
	//	}

		// make sure we aren't trying to compute fuel variables using only first returns for metrics
	if (m_UseFirstReturnsForAllMetrics && m_ApplyFuelModels) {
		printf("Cannot compute fuel parameters using only first returns for metrics\n");
		printf("/fuel cannot be used with /first\n");
		return(1);
	}

	// make sure we aren't trying to compute strata metrics when not doing intensity metrics
	if (m_NoItensityMetrics && (m_DoHeightStrataIntensity || m_DoHeightStrata)) {
		printf("Cannot compute strata metrics without computing intensity metrics...\n");
		printf("/strata and /intstrata cannot be used with /nointensity.\n");
		return(1);
	}

	return(nRetCode);
}

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
#ifdef FUSION_PORTABLE
	FusionSetCommandLine(argc, argv);
	m_clp.SetCommandLineParameters(GetCommandLine());
#endif
	float Fraction;
	int WholePart;
	float Percentile[21];
	int PercentileNeeded[] = { 1, 2, 4, 5, 6, 8, 10, 12, 14, 15, 16, 18, 19 };		// 5% classes
	int PercentileCount = 13;
	float SpecialElevPercentile[101];
	int Bins[NUMBEROFBINS];
	int TheBin;
	CFileSpec TempFS;
	BOOL AllFilesExist;
	int FileAccessAttemptCount;
	BOOL FileAccessAttemptError;
	time_t teststart;
	time_t testtime;
	int i, j, k, l, m, p;

	// initialize MFC and print and error on failure
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		// TODO: change error code to suit your needs
		std::cerr << _T("Fatal Error: MFC initialization failed") << endl;
		m_nRetCode = 1;
	}
	else
	{
		// initialize toolkit variables...if return value is FALSE, /version was on the command line
		// with /version, only the program name and version information are output
		if (!LTKCL_Initialize()) {
			//			// free up memory for ground models...should not have been intitialized at this point since we haven't parsed the command line
			//			if (m_UseGround && m_GroundDTM != NULL)
			//				delete [] m_GroundDTM;

			return(0);
		}

		InitializeGlobalVariables();

		m_nRetCode = ParseCommandLine();

		m_nRetCode |= PresentInteractiveDialog();

		// attempt to load LAZ/LAS dll
		if (!m_NoLASzip_DLL)
			laszip_load_dll();



		//// testing code
		//// get amount of available memory
		//MEMORYSTATUSEX statex;
		//PointRecord* Points = NULL;
		//_int64 PointCount = 0;
		//
		//#define DIV 1024/1024
		//#define WIDTH 7
		//
		//statex.dwLength = sizeof(statex);
		//GlobalMemoryStatusEx(&statex);
		//
		//_tprintf(TEXT("There is  %*ld percent of memory in use.\n"),
		//	WIDTH, statex.dwMemoryLoad);
		//_tprintf(TEXT("There are %*I64d total GB of physical memory.\n"),
		//	WIDTH, statex.ullTotalPhys / DIV);
		//_tprintf(TEXT("There are %*I64d free  GB of physical memory.\n"),
		//	WIDTH, statex.ullAvailPhys / DIV);
		//_tprintf(TEXT("There are %*I64d total GB of paging file.\n"),
		//	WIDTH, statex.ullTotalPageFile / DIV);
		//_tprintf(TEXT("There are %*I64d free  GB of paging file.\n"),
		//	WIDTH, statex.ullAvailPageFile / DIV);
		//_tprintf(TEXT("There are %*I64d total GB of virtual memory.\n"),
		//	WIDTH, statex.ullTotalVirtual / DIV);
		//_tprintf(TEXT("There are %*I64d free  GB of virtual memory.\n"),
		//	WIDTH, statex.ullAvailVirtual / DIV);
		//
		//for (i = 1; i < 4096; i++) {
		//	// test allocation
		//	PointCount = (_int64) i * 100000000;
		//
		//	if (statex.ullAvailVirtual / (size_t)16 > (size_t)PointCount + (size_t)10000) {
		//		// allocate memory for a list of points
		//		try {
		//			Points = new PointRecord[(size_t)PointCount];
		//		}
		//		catch (...) {
		//			Points = NULL;
		//		}
		//	}
		//
		//	if (Points != NULL) {
		//		printf("Allocation of %i * 100M points successful\r\n", i);
		//
		//		// try to use the array
		//		Points[((_int64) i * 100000000) - 1].CellNumber = 10;
		//
		//		delete[] Points;
		//		Points = NULL;
		//	}
		//	else {
		//		printf("***Allocation of %i * 100M points failed\r\n", i);
		//	}
		//}
		//
		//return(1);



				// &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&









				// do the processing
				// ********************************************************************************************************
				// ********************************************************************************************************
		CLidarData ldat;
		if (m_DataFileCount && !m_nRetCode) {
			// print status info
			LTKCL_PrintProgramHeader();
			LTKCL_PrintCommandLine();
			LTKCL_PrintRunTime();

			// look for bad switches on command line
			LTKCL_VerifyCommandLine(ValidCommandLineSwitches);

			// test for missing DTM files...when /failnoz is specified, this is a hard failure
			if (m_TestTerrain) {
				if (m_UseGround && !m_Terrain.IsValid()) {
					m_csStatus.Format("Problems with the ground surface model(s): %s", (LPCSTR) m_GroundFileName);
					LTKCL_PrintStatus(m_csStatus);

					m_nRetCode = 3;
				}
			}

			if (!m_nRetCode) {
				// check data files to make sure they exist...this should have already been checked
				AllFilesExist = TRUE;
				for (i = 0; i < m_DataFileCount; i++) {
					TempFS.SetFullSpec(m_DataFile[i]);
					if (!TempFS.Exists()) {
						m_csStatus.Format("Data file does not exist: %s", (LPCSTR) m_DataFile[i]);
						LTKCL_PrintVerboseStatus(m_csStatus);
						AllFilesExist = FALSE;
					}
				}

				// 5/17/2022 adding check for valid format for point files...designed to skip files that match
				// a wildcard specifier but are not actually point files (e.g. tile.lasx)
				if (AllFilesExist) {
					// work backwards through the list so we can remove items without rearranging the part of the list
					// not yet processed
					int removedFileCount = 0;
					for (i = m_DataFileCount - 1; i >= 0; i--) {
						if (!LTKCL_VerifyPointFileFormatIsValid(m_DataFile[i])) {
							m_csStatus.Format("Removing invalid point file from input list: %s", (LPCSTR) m_DataFile[i]);
							LTKCL_PrintStatus(m_csStatus);

							// remove file from list
							m_DataFile.RemoveAt(i);

							removedFileCount++;
						}
					}
					m_DataFileCount -= removedFileCount;
				}

#define		MAXFILEATTEMPTS		5
#define		FILEACCESSDELAY		10

				BOOL LASHasRGB = TRUE;
				BOOL LASHasNIR = TRUE;
				if (AllFilesExist) {

					// check data files to make sure we can open them
					FileAccessAttemptCount = 0;			// try up to 5 times with a delay between attempts
					while (FileAccessAttemptCount < MAXFILEATTEMPTS) {
						FileAccessAttemptError = FALSE;
						for (i = 0; i < m_DataFileCount; i++) {
							ldat.Open(m_DataFile[i]);
							if (!ldat.IsValid()) {
								m_csStatus.Format("Can't open data file: %s", (LPCSTR) m_DataFile[i]);
								LTKCL_PrintVerboseStatus(m_csStatus);

								// get error message from CLidarData::Open()
								// mainly focused on trying to read compressed data (LAZ) without laszip.dll
								// but may catch other problems as well.
								ldat.GetErrorMessage(m_csStatus);
								LTKCL_PrintVerboseStatus(m_csStatus);

								// set flag
								FileAccessAttemptError = TRUE;
							}
							else {
								// check for LAS format if including specific classes
								if (m_IncludeSpecificClasses) {
									if (ldat.GetFileFormat() != LASDATA) {
										m_IncludeSpecificClasses = FALSE;
									}
								}

								if (m_DoRGBMetrics) {
									if (ldat.GetFileFormat() != LASDATA) {
										LASHasRGB = FALSE;
										LASHasNIR = FALSE;
									}
									else {
										//										if (ldat.m_LASFile.Header.Version < 1.2) {
										//											LASHasRGB = FALSE;
										//										}
										//										else if (ldat.m_LASFile.Header.PointDataFormatID != 2 && ldat.m_LASFile.Header.PointDataFormatID != 3 && ldat.m_LASFile.Header.PointDataFormatID != 5) {
#ifdef USE_LASLIB
										if (ldat.lasreader->point.have_rgb) {
											LASHasRGB = FALSE;
										}
#else
										if (ldat.m_HaveLASZIP_DLL) {
											if (ldat.lasdll_header->point_data_format != 2 &&
												ldat.lasdll_header->point_data_format != 3 &&
												ldat.lasdll_header->point_data_format != 5 &&
												ldat.lasdll_header->point_data_format != 7 &&
												ldat.lasdll_header->point_data_format != 8 &&
												ldat.lasdll_header->point_data_format != 10
												) {
												LASHasRGB = FALSE;
												LASHasNIR = FALSE;
											}

											if (m_DoNIRMetrics && ldat.lasdll_header->point_data_format != 8 && ldat.lasdll_header->point_data_format != 10)
												LASHasNIR = FALSE;
										}
										else {
											if (ldat.m_LASFile.Header.PointDataFormatID != 2 &&
												ldat.m_LASFile.Header.PointDataFormatID != 3 &&
												ldat.m_LASFile.Header.PointDataFormatID != 5 &&
												ldat.m_LASFile.Header.PointDataFormatID != 7 &&
												ldat.m_LASFile.Header.PointDataFormatID != 8 &&
												ldat.m_LASFile.Header.PointDataFormatID != 10
												) {
												LASHasRGB = FALSE;
												LASHasNIR = FALSE;
											}
											if (m_DoNIRMetrics && ldat.m_LASFile.Header.PointDataFormatID != 8 && ldat.m_LASFile.Header.PointDataFormatID != 10)
												LASHasNIR = FALSE;
										}
#endif
									}
								}
								ldat.Close();
							}
						}

						// if no error, jump out
						if (!FileAccessAttemptError)
							break;

						// delay and try to access files again
						time(&teststart);                 // Get time in seconds
						while (TRUE) {
							time(&testtime);                 // Get time in seconds
							if (testtime - teststart >= FILEACCESSDELAY) {
								m_csStatus.Format("File access time delay loop: %i of %i", FileAccessAttemptCount + 1, MAXFILEATTEMPTS);
								LTKCL_PrintStatus(m_csStatus);

								break;
							}
						}

						FileAccessAttemptCount++;
					}

					// set flag for error
					if (FileAccessAttemptError) {
						LTKCL_PrintStatus("Could not open some input data files");
						m_nRetCode = 2;
					}
				}
				else {
					LTKCL_PrintStatus("Some input data files do not exist");
					m_nRetCode = 2;
				}

				if (m_DoRGBMetrics & !LASHasRGB) {
					m_csStatus.Format("To use the /RGB switch, data files must be LAS version 1.2 or newer and contain RGB color values for each return.\n(Point data record formats 2, 3, 5, 7, 8, or 10).\nSome of the data files do not meet this requirement.");
					LTKCL_PrintStatus(m_csStatus);

					m_nRetCode = 20;
				}

				if (m_DoNIRMetrics & !LASHasNIR) {
					m_csStatus.Format("To use the /RGB switch with NIR values, data files must be LAS version 1.4 or newer and contain NIR values for each return.\n(Point data record formats 8 or 10).\nSome of the data files do not meet this requirement.");
					LTKCL_PrintStatus(m_csStatus);

					m_nRetCode = 20;
				}

				// check ground DTM
				if (m_UseGround && !m_Terrain.IsValid()) {
					m_csStatus.Format("Problems with the ground surface model(s): %s", (LPCSTR) m_GroundFileName);
					LTKCL_PrintStatus(m_csStatus);

					m_nRetCode = 3;
					m_UseGround = FALSE;
				}

				// check canopy DTM
				if (m_UseCanopy && !m_CanopyDTM.IsValid()) {
					m_csStatus.Format("Can't open the canopy model: %s", (LPCSTR) m_CanopyFileName);
					LTKCL_PrintStatus(m_csStatus);
					m_nRetCode = 4;			// disable to allow processing without canopy
				}

				//	moved up 4/16/2010
				//				// print status info
				//				LTKCL_PrintProgramHeader();
				//				LTKCL_PrintCommandLine();
				//				LTKCL_PrintRunTime();

				if (!m_nRetCode) {			// 11/10/2010 very redundent but I'm too lazy to change the structure when everything is working
					// echo strata info
					// verbose status with strata...don't print the last one
					char ts[1024];
					if (m_DoHeightStrata) {
						sprintf(ts, "Elevation strata:");

						for (int i = 0; i < m_HeightStrataCount - 1; i++) {
							sprintf(ts, "%s %.2lf", ts, m_HeightStrata[i]);
						}
						LTKCL_PrintVerboseStatus(ts);
					}


					// verbose status with strata...don't print the last one
					if (m_DoHeightStrataIntensity) {
						sprintf(ts, "Intensity strata:");

						for (int i = 0; i < m_HeightStrataIntensityCount - 1; i++) {
							sprintf(ts, "%s %.2lf", ts, m_HeightStrataIntensity[i]);
						}
						LTKCL_PrintVerboseStatus(ts);
					}

					// get number of points in file and data ranges
					double xMax = -FLT_MAX;
					double xMin = FLT_MAX;
					double yMax = -FLT_MAX;
					double yMin = FLT_MAX;
					//					double zMax=-FLT_MAX;
					//					double zMin=FLT_MAX;
					LIDARRETURN pt;

					double Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax;
					if (m_UserGrid) {
						double adjustedwidth = ceil(m_CustomGridWidth / m_CellSize) * m_CellSize;
						double adjustedheight = ceil(m_CustomGridHeight / m_CellSize) * m_CellSize;

						Custom_xMin = m_CustomOriginX;
						Custom_yMin = m_CustomOriginY;
						Custom_xMax = Custom_xMin + adjustedwidth;
						Custom_yMax = Custom_yMin + adjustedheight;
					}
					else if (m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
						if (m_ForceExtent) {
							// adjust global custom min/max so origin is always a multiple of the spacing
							// 3/14/2022 fix logic so it works with negative X and Y values
							if (fmod(m_CustomOriginX, m_CellSize) > 0.0) {
								m_CustomOriginX = m_CustomOriginX - fmod(m_CustomOriginX, m_CellSize);
							} else if (fmod(m_CustomOriginX, m_CellSize) < 0.0) {
								m_CustomOriginX = m_CustomOriginX - (m_CellSize + fmod(m_CustomOriginX, m_CellSize));
							}
							if (fmod(m_CustomOriginY, m_CellSize) > 0.0) {
								m_CustomOriginY = m_CustomOriginY - fmod(m_CustomOriginY, m_CellSize);
							} else if (fmod(m_CustomOriginY, m_CellSize) < 0.0) {
								m_CustomOriginY = m_CustomOriginY - (m_CellSize + fmod(m_CustomOriginY, m_CellSize));
							}
						}

						double adjustedwidth = ceil((m_CustomMaxX - m_CustomOriginX) / m_CellSize) * m_CellSize;
						double adjustedheight = ceil((m_CustomMaxY - m_CustomOriginY) / m_CellSize) * m_CellSize;

						Custom_xMin = m_CustomOriginX;
						Custom_yMin = m_CustomOriginY;
						Custom_xMax = Custom_xMin + adjustedwidth;
						Custom_yMax = Custom_yMin + adjustedheight;
					}

					if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
						// compute the buffer size in cells (round up)...this seemed necessary as I was
						// having trouble with "off by 1" errors when outputing grid cells and labeling them
						// correctly in the CSV output
						if (m_AddBufferDistance)
							m_BufferCells = (int)ceil(m_BufferDistance / m_CellSize);

						// make adjustments for the buffer area...add to the extent
						// At this point in the code, the extent is used to figure out which data tiles
						// have point within the area of interest. The actual extent will be computed
						// from the points within the area of interest and will include the buffer area
						if (m_AddBufferDistance || m_AddBufferCells) {
							Custom_xMin -= (m_BufferCells * m_CellSize);
							Custom_yMin -= (m_BufferCells * m_CellSize);
							Custom_xMax += (m_BufferCells * m_CellSize);
							Custom_yMax += (m_BufferCells * m_CellSize);
						}
					}

					// 5/14/2024 Notes regarding use of indec files and COPC format
					// The index files are being used to get the extent of a point file. This isn't really helping with LAS/LAZ format data
					// but does help with LDA or other formats since these formats don't include the extent of the data in a header.
					// I don't read the data through the index because it slows down reading. For LAZ, the performance penalty is heavy.
					// For LAS format, not so bad but still slows things down since we are basically bounding around in the file when reading points 
					// compared to a simple sequential read.
					//
					// For COPC format, we can take advantage of the indexing to read specific point blocks from the LAZ data. This should give
					// good performance and offers the ability to work with very large LAZ files in COPC format. In contrast, sequential reads
					// of large LAZ files take a long time. Worst case is an entire lidar project delivered in a single file. Even for 
					// a small AOI, you will have to read the entire file. With the COPC indexing in play, only a few chunks of data
					// need be read.

					// figure out extent of data...use all returns for get extents but only count first returns if using m_UseFirstReturnsForDensity
					// if using a custom grid origin and size, we have to read all the points to see how many fall
					// in the grid area
					_int64 PointCount = 0;
					_int64 FilePointCount = 0;
					CDataIndex Index;
					//					long offset;
					for (i = 0; i < m_DataFileCount; i++) {
						// if using a custom grid...see if the file has points that are within the grid area
						// using the extent for the file (if indexed or LAS)
						if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
							// modified 6/9/2008		if (Index.Open(m_DataFile[i]) && !m_UserGrid && !m_UserGridXY) {		// need to get height if normalizing to ground
							if (Index.Open(m_DataFile[i])) {		// need to get height if normalizing to ground
								// compare the extent with the grid area...if they overlap, read each point and test
								if (RectanglesIntersect(Index.m_Header.MinX, Index.m_Header.MinY, Index.m_Header.MaxX, Index.m_Header.MaxY, Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax)) {
									//								if ((Index.m_Header.MinX < Custom_xMin && Index.m_Header.MaxX >= Custom_xMin) ||
									//									(Index.m_Header.MinX < Custom_xMax && Index.m_Header.MaxX >= Custom_xMax) ||
									//									(Index.m_Header.MinY < Custom_yMin && Index.m_Header.MaxY >= Custom_yMin) ||
									//									(Index.m_Header.MinY < Custom_yMax && Index.m_Header.MaxY >= Custom_yMax)) {
																		// read and test each point
									Index.Close();
								}
								else {
									// no points in file are within grid area...go to next file
									Index.Close();
									continue;
								}
							}

							ldat.Open(m_DataFile[i]);
							if (ldat.IsValid()) {
								FilePointCount = 0;

								// see if file is LAS format
								if (ldat.GetFileFormat() == LASDATA) {		// && !m_UseGround if you need min/max Z
									// compare the extent with the grid area...if they overlap, read each point and test
#ifdef USE_LASLIB
									if (!RectanglesIntersect(ldat.lasreader->get_min_x(), ldat.lasreader->get_min_y(), ldat.lasreader->get_max_x(), ldat.lasreader->get_max_y(), Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax)) {
										ldat.Close();
										continue;
									}
#else
									if (ldat.m_HaveLASZIP_DLL) {
										if (!RectanglesIntersect(ldat.lasdll_header->min_x, ldat.lasdll_header->min_y, ldat.lasdll_header->max_x, ldat.lasdll_header->max_y, Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax)) {
											ldat.Close();
											continue;
										}
									}
									else {
										if (!RectanglesIntersect(ldat.m_LASFile.Header.MinX, ldat.m_LASFile.Header.MinY, ldat.m_LASFile.Header.MaxX, ldat.m_LASFile.Header.MaxY, Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax)) {
											ldat.Close();
											continue;
										}
									}
#endif
								}

								// at this point we know the file has an extent that overlaps the custom area
								// read using index files (if available) and use the custom extent
								// @#$%^& using index files slows things down by a factor of 3!!
/*								if (Index.Open(m_DataFile[i])) {
									// read point using the index and the custom extent
									while ((offset = Index.JumpToNextPointInRange(Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax)) >= 0) {
										ldat.SetPosition(offset, SEEK_SET);
										ldat.ReadNextRecord(&pt);

										PointCount ++;
										FilePointCount ++;

										xMax = max(pt.X, xMax);
										xMin = min(pt.X, xMin);
										yMax = max(pt.Y, yMax);
										yMin = min(pt.Y, yMin);
									}

									// close the index
									Index.Close();
								}
								else {
*/									
								// check for COPC format and register extent
								if (ldat.IsCOPC()) {
									uint64_t chunkCount;

									ldat.RegisterCOPCExtent(Custom_xMin, Custom_yMin, Custom_xMax, Custom_yMax);

									// see if there are points in the sample...if none go to next file
									chunkCount = ldat.QueryForCOPCChunks();

									if (chunkCount <= 0) {
										ldat.Close();
										continue;
									}
								}

								 
								// read and test each point
								while (TRUE) {		// && !m_UseGround if you need min/max Z
									if (ldat.IsCOPC()) {
										if (!ldat.ReadNextCOPCRecord(&pt))
											break;
									}
									else {
										if (!ldat.ReadNextRecord(&pt))
											break;
									}

									if (ldat.LastPointIsWithheld())
										continue;

									if (ldat.LastPointIsOverlap() && m_SkipOverlapPoints)
										continue;

									// if using a custom grid, see if the point is within the grid area
									if (pt.X < Custom_xMin || pt.X > Custom_xMax || pt.Y < Custom_yMin || pt.Y > Custom_yMax)
										continue;
									//									if (m_UseFirstReturnsForDensity && pt.ReturnNumber == 1) {
									//										PointCount ++;
									//										FilePointCount ++;
									//									}
									//									else if (!m_UseFirstReturnsForDensity) {
									PointCount++;
									FilePointCount++;
									//									}
									xMax = max(pt.X, xMax);
									xMin = min(pt.X, xMin);
									yMax = max(pt.Y, yMax);
									yMin = min(pt.Y, yMin);
								}
								//								}

																// close the LIDAR data file
								ldat.Close();
							}
						}
						else {
							// sequential read of all points
							if (m_IncludeSpecificClasses && m_NumberOfClassCodes > 0) {
								ldat.Open(m_DataFile[i]);
								if (ldat.IsValid()) {
									// see if file is LAS format
									if (ldat.GetFileFormat() == LASDATA) {
										FilePointCount = 0;
#ifdef USE_LASLIB
										xMax = max(xMax, ldat.lasreader->get_max_x());
										xMin = min(xMin, ldat.lasreader->get_min_x());
										yMax = max(yMax, ldat.lasreader->get_max_y());
										yMin = min(yMin, ldat.lasreader->get_min_y());
#else
										if (ldat.m_HaveLASZIP_DLL) {
											xMax = max(xMax, ldat.lasdll_header->max_x);
											xMin = min(xMin, ldat.lasdll_header->min_x);
											yMax = max(yMax, ldat.lasdll_header->max_y);
											yMin = min(yMin, ldat.lasdll_header->min_y);
										}
										else {
											xMax = max(xMax, ldat.m_LASFile.Header.MaxX);
											xMin = min(xMin, ldat.m_LASFile.Header.MinX);
											yMax = max(yMax, ldat.m_LASFile.Header.MaxY);
											yMin = min(yMin, ldat.m_LASFile.Header.MinY);
										}
#endif

										// read each point looking at classification codes
										while (ldat.ReadNextRecord(&pt)) {
											if (ldat.LastPointIsWithheld())
												continue;

											if (ldat.LastPointIsOverlap() && m_SkipOverlapPoints)
												continue;

											// test for specific LAS classification values
#ifdef USE_LASLIB
											if (ldat.lasreader->point.classification > 0 && ldat.lasreader->point.classification < NUMBER_OF_CLASS_CODES) {
												if (m_ClassCodes[ldat.lasreader->point.classification] == 1) {
													PointCount++;
													FilePointCount++;
												}
											}
#else
											if (ldat.m_HaveLASZIP_DLL) {
												if (ldat.lasdll_point->classification > 0 && ldat.lasdll_point->classification < NUMBER_OF_CLASS_CODES) {
													if (m_ClassCodes[ldat.lasdll_point->classification] == 1) {
														PointCount++;
														FilePointCount++;
													}
												}
											}
											else {
												if (ldat.m_LASFile.PointRecord.Classification > 0 && ldat.m_LASFile.PointRecord.Classification < NUMBER_OF_CLASS_CODES) {
													if (m_ClassCodes[ldat.m_LASFile.PointRecord.Classification] == 1) {
														PointCount++;
														FilePointCount++;
													}
												}
											}
#endif
										}
									}
									ldat.Close();

									m_csStatus.Format("   %s: %I64i points", (LPCSTR) m_DataFile[i], FilePointCount);
									LTKCL_PrintStatus(m_csStatus);

									continue;
								}
							}

							// the metrics are being computed for the extent of the data
							if (Index.Open(m_DataFile[i])) {		// need to get height if normalizing to ground
								xMin = min(Index.m_Header.MinX, xMin);
								yMin = min(Index.m_Header.MinY, yMin);
								//						zMin = min(Index.m_Header.MinZ, zMin);
								xMax = max(Index.m_Header.MaxX, xMax);
								yMax = max(Index.m_Header.MaxY, yMax);
								//						zMax = max(Index.m_Header.MaxZ, zMax);

								PointCount += (_int64)Index.m_Header.TotalPointsIndexed;
								FilePointCount = (_int64)Index.m_Header.TotalPointsIndexed;

								Index.Close();
							}
							else {
								ldat.Open(m_DataFile[i]);
								if (ldat.IsValid()) {
									FilePointCount = 0;

									// see if file is LAS format
									if (ldat.GetFileFormat() == LASDATA) {		// && !m_UseGround if you need min/max Z
#ifdef USE_LASLIB
										xMax = max(xMax, ldat.lasreader->get_max_x());
										xMin = min(xMin, ldat.lasreader->get_min_x());
										yMax = max(yMax, ldat.lasreader->get_max_y());
										yMin = min(yMin, ldat.lasreader->get_min_y());

										PointCount += ldat.lasreader->npoints;
										FilePointCount = ldat.lasreader->npoints;
#else
										if (ldat.m_HaveLASZIP_DLL) {
											xMax = max(xMax, ldat.lasdll_header->max_x);
											xMin = min(xMin, ldat.lasdll_header->min_x);
											yMax = max(yMax, ldat.lasdll_header->max_y);
											yMin = min(yMin, ldat.lasdll_header->min_y);
											//								zMax = max(zMax, ldat.lasdll_header->max_z);
											//								zMin = min(zMin, ldat.lasdll_header->min_z);

											if (ldat.lasdll_header->version_minor >= 4) {
												PointCount += ldat.lasdll_header->extended_number_of_point_records;
												FilePointCount = ldat.lasdll_header->extended_number_of_point_records;
											}
											else {
												PointCount += ldat.lasdll_header->number_of_point_records;
												FilePointCount = ldat.lasdll_header->number_of_point_records;
											}
										}
										else {
											xMax = max(xMax, ldat.m_LASFile.Header.MaxX);
											xMin = min(xMin, ldat.m_LASFile.Header.MinX);
											yMax = max(yMax, ldat.m_LASFile.Header.MaxY);
											yMin = min(yMin, ldat.m_LASFile.Header.MinY);
											//								zMax = max(zMax, ldat.m_LASFile.Header.MaxZ);
											//								zMin = min(zMin, ldat.m_LASFile.Header.MinZ);

											PointCount += ldat.m_LASFile.Header.NumberOfPointRecords;
											FilePointCount = ldat.m_LASFile.Header.NumberOfPointRecords;
										}
#endif
									}
									else {
										while (ldat.ReadNextRecord(&pt)) {		// && !m_UseGround if you need min/max Z
											// if using a custom grid, see if the point is within the grid area
		//									if (m_UseFirstReturnsForDensity && pt.ReturnNumber == 1) {
		//										PointCount ++;
		//										FilePointCount ++;
		//									}
		//									else if (!m_UseFirstReturnsForDensity) {
											PointCount++;
											FilePointCount++;
											//									}
											xMax = max(pt.X, xMax);
											xMin = min(pt.X, xMin);
											yMax = max(pt.Y, yMax);
											yMin = min(pt.Y, yMin);
										}
									}
									ldat.Close();
								}
							}
						}
						// report total number of points in each file
						if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent)
							m_csStatus.Format("   %s: %I64i points within grid area", (LPCSTR) m_DataFile[i], FilePointCount);
						else
							m_csStatus.Format("   %s: %I64i points", (LPCSTR) m_DataFile[i], FilePointCount);
						LTKCL_PrintStatus(m_csStatus);
					}

					// report total number of points in all files if using more than 1 data file
					if (m_DataFileCount > 1) {
						if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent)
							m_csStatus.Format("   Total: %I64i points within grid area\n", PointCount);
						else
							m_csStatus.Format("   Total: %I64i points\n", PointCount);
						LTKCL_PrintStatus(m_csStatus);
					}

					//					// list ground surface models if using verbose mode
					//					if (m_UseGround) {
					//						// check all models
					//						for (i = 0; i < m_GroundFileCount; i ++) {
					//							if (m_GroundDTM[i].IsValid()) {
					//								// output verbose info...list ground surface files
					//								m_csStatus.Format("   Using surface model (%i of %i): %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
					//								LTKCL_PrintVerboseStatus(m_csStatus);
					//							}
					//						}
					//					}

					if (PointCount) {
						// if using a custom grid size, adjust extent
						if (m_UserGrid) {
							double adjustedwidth = ceil(m_CustomGridWidth / m_CellSize) * m_CellSize;
							double adjustedheight = ceil(m_CustomGridHeight / m_CellSize) * m_CellSize;

							xMin = m_CustomOriginX;
							yMin = m_CustomOriginY;
							xMax = xMin + adjustedwidth;
							yMax = yMin + adjustedheight;

							//							// adjust for buffer strip
							//							if (m_AddBufferDistance) {
							//								xMin -= m_BufferDistance;
							//								yMin -= m_BufferDistance;
							//								xMax += m_BufferDistance;
							//								yMax += m_BufferDistance;
							//							}

							if (m_AddBufferDistance || m_AddBufferCells) {
								xMin -= (m_BufferCells * m_CellSize);
								yMin -= (m_BufferCells * m_CellSize);
								xMax += (m_BufferCells * m_CellSize);
								yMax += (m_BufferCells * m_CellSize);
							}
						}
						else if (m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
							double adjustedwidth = ceil((m_CustomMaxX - m_CustomOriginX) / m_CellSize) * m_CellSize;
							double adjustedheight = ceil((m_CustomMaxY - m_CustomOriginY) / m_CellSize) * m_CellSize;

							xMin = m_CustomOriginX;
							yMin = m_CustomOriginY;
							xMax = xMin + adjustedwidth;
							yMax = yMin + adjustedheight;

							//							// adjust for buffer strip
							//							if (m_AddBufferDistance) {
							//								xMin -= m_BufferDistance;
							//								yMin -= m_BufferDistance;
							//								xMax += m_BufferDistance;
							//								yMax += m_BufferDistance;
							//							}

							if (m_AddBufferDistance || m_AddBufferCells) {
								xMin -= (m_BufferCells * m_CellSize);
								yMin -= (m_BufferCells * m_CellSize);
								xMax += (m_BufferCells * m_CellSize);
								yMax += (m_BufferCells * m_CellSize);
							}
						}
						else {
							// figure out origin and size for surface model
							// adjust min/max so origin is always a multiple of the spacing
							// 3/14/2022 modifying code to deal with negative X or Y values. Old logic did not work in this case
							if (fmod(xMin, m_CellSize) > 0.0) {
								xMin = xMin - fmod(xMin, m_CellSize);
							} else if (fmod(xMin, m_CellSize) < 0.0) {
								xMin = xMin - (m_CellSize + fmod(xMin, m_CellSize));
							}

							if (fmod(yMin, m_CellSize) > 0.0) {
								yMin = yMin - fmod(yMin, m_CellSize);
							} else if (fmod(yMin, m_CellSize) < 0.0) {
								yMin = yMin - (m_CellSize + fmod(yMin, m_CellSize));
							}

							if (fmod(xMax, m_CellSize) > 0.0) {
								xMax = xMax + (m_CellSize - fmod(xMax, m_CellSize));
							} else if (fmod(xMax, m_CellSize) < 0.0) {
								xMax = xMax - fmod(xMax, m_CellSize);
							}

							if (fmod(yMax, m_CellSize) > 0.0) {
								yMax = yMax + (m_CellSize - fmod(yMax, m_CellSize));
							} else if (fmod(yMax, m_CellSize) < 0.0) {
								yMax = yMax - fmod(yMax, m_CellSize);
							}

							// no adjustment for buffer needed...extent of area stored in final output
							// will be adjusted to an area smaller than the data extent
						}
						double width = (xMax - xMin);
						double height = (yMax - yMin);

						// changed 4/2/2008 to add 1/2 the cell size to the extent...
						// the net result is to round up the number of rows & columns
						// we should never compute a col,row value for a point where the 
						// col or row values are equal to cols or rows
						int cols = (int)((xMax - xMin + m_CellSize / 2.0) / m_CellSize) + 1;
						int rows = (int)((yMax - yMin + m_CellSize / 2.0) / m_CellSize) + 1;
						//						int cols = (int) ((xMax - xMin) / m_CellSize) + 1;
						//						int rows = (int) ((yMax - yMin) / m_CellSize) + 1;

												// width and height should be a multiple of the cell size and minx,miny should fall on a multiple of the cell size

												// *****buffer: we now have either a custom extent with the buffer strip added or
												// the full extent of all the data...as cells are processed, we will check to see if
												// their metrics should be output. for grids, the buffer area will be removed when the 
												// grid is stored. For buffers specified as an absolute width, the area stored may
												// not correspond to the custom grid or data extent as the buffer width may not
												// be an even multiple of the cell size

												// get amount of available memory
						MEMORYSTATUS stat;
						GlobalMemoryStatus(&stat);
						PointRecord* Points = NULL;

						//						m_csStatus.Format("   Available memory: %I64u", stat.dwAvailVirtual);
						//						LTKCL_PrintVerboseStatus(m_csStatus);

												// check available memory against number of points
						if (stat.dwAvailVirtual / (size_t)16 > (size_t)PointCount + (size_t)10000) {
							// allocate memory for a list of points
							try {
								Points = new PointRecord[(size_t)PointCount];
							}
							catch (...) {
								Points = NULL;
							}
						}

						// try to read the ground models that cover the data extent into memory
						// using the patch access for surfaces from disk takes 4-5 times longer to process
						if (Points && m_UseGround && m_Terrain.IsValid()) {
							// 11/28/2011 changed to give control over creation of internal ground model
							m_Terrain.PreLoadData(xMin, yMin, xMax, yMax, m_CreateInternalDTM);
							// 11/29/2011							m_Terrain.PreLoadData(xMin, yMin, xMax, yMax, TRUE);
							//							m_Terrain.PreLoadData(xMin, yMin, xMax, yMax, FALSE);

														// print verbose info
							int ModelsInMemory = 0;
							for (i = 0; i < m_Terrain.GetModelCount(); i++) {
								if (m_Terrain.AreModelElevationsInMemory(i))
									ModelsInMemory++;
							}

							if (ModelsInMemory) {
								m_csStatus.Format("Loaded %i ground model(s) into memory:", ModelsInMemory);
								LTKCL_PrintVerboseStatus(m_csStatus);
								for (i = 0; i < m_Terrain.GetModelCount(); i++) {
									if (m_Terrain.AreModelElevationsInMemory(i)) {
										m_csStatus.Format("   %s (%i of %i)", (LPCSTR) m_Terrain.GetModelFileName(i), i + 1, m_GroundFileCount);
										LTKCL_PrintVerboseStatus(m_csStatus);
									}
								}
							}
							else if (m_Terrain.InternalModelIsValid()) {
								int ModelsUsedForInternal = 0;
								for (i = 0; i < m_Terrain.GetModelCount(); i++) {
									if (m_Terrain.WasModelUsedForInternalModel(i))
										ModelsUsedForInternal++;
								}

								if (ModelsUsedForInternal) {
									m_csStatus.Format("Used %i ground model(s) to build internal model:", ModelsUsedForInternal);
									LTKCL_PrintVerboseStatus(m_csStatus);
									for (i = 0; i < m_Terrain.GetModelCount(); i++) {
										if (m_Terrain.WasModelUsedForInternalModel(i)) {
											m_csStatus.Format("   %s (%i of %i)", (LPCSTR) m_Terrain.GetModelFileName(i), i + 1, m_GroundFileCount);
											LTKCL_PrintVerboseStatus(m_csStatus);
										}
									}
								}
								LTKCL_PrintVerboseStatus("");
								//								m_csStatus.Format("   Created internal model for analysis extent:\n   %s", m_Terrain.InternalModelDescription());
								//								LTKCL_PrintVerboseStatus(m_csStatus);
							}
						}
						/*						if (Points && m_UseGround) {
													if (!m_GroundFromDisk) {
														for (i = 0; i < m_GroundFileCount; i ++) {
															if (RectanglesIntersect(m_GroundDTM[i].OriginX(), m_GroundDTM[i].OriginY(), m_GroundDTM[i].OriginX() + m_GroundDTM[i].Width(), m_GroundDTM[i].OriginY() + m_GroundDTM[i].Height(), xMin, yMin, xMax, yMax)) {
																m_GroundDTM[i].LoadElevations(m_GroundFiles[i], TRUE, FALSE);	// read elevations into memory

																// if full read failed, set up for patch access
																if (!m_GroundDTM[i].IsValid()) {
																	m_GroundDTM[i].LoadElevations(m_GroundFiles[i]);		// patch access

																	m_csStatus.Format("   Surface model (%i of %i) accessed from disk: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
																}
																else {
																	m_csStatus.Format("   Loaded surface model (%i of %i) into memory: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
																}
																LTKCL_PrintVerboseStatus(m_csStatus);
															}
														}
													}
													else {
														for (i = 0; i < m_GroundFileCount; i ++) {
															if (RectanglesIntersect(m_GroundDTM[i].OriginX(), m_GroundDTM[i].OriginY(), m_GroundDTM[i].OriginX() + m_GroundDTM[i].Width(), m_GroundDTM[i].OriginY() + m_GroundDTM[i].Height(), xMin, yMin, xMax, yMax)) {
																m_csStatus.Format("   Surface model (%i of %i) accessed from disk: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
																LTKCL_PrintVerboseStatus(m_csStatus);
															}
														}
													}
												}
						*/
						BOOL UseIndex;
						int col, row;
						float groundelev;
						//						float canopyelev;
						_int64 FirstReturns = 0;
						PointCount = 0;
						int TempPointCount;
						BOOL CellInBuffer = FALSE;
						int CellOffsetRows = 0;
						int CellOffsetCols = 0;

						// added to track actual start and stop row/col output when using distance buffer
						int MinRowOutput = rows;
						int MaxRowOutput = 0;
						int MinColOutput = cols;
						int MaxColOutput = 0;

						if (Points) {
							// read the all data into the list except gross outlier points
							for (i = 0; i < m_DataFileCount; i++) {
								UseIndex = FALSE;

								// check the data extent against the analysis area...only read returns when we have to
								if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
									if (Index.Open(m_DataFile[i])) {		// need to get height if normalizing to ground
										// compare the extent with the grid area...if they overlap, read each point and test
										if (RectanglesIntersect(Index.m_Header.MinX, Index.m_Header.MinY, Index.m_Header.MaxX, Index.m_Header.MaxY, xMin, yMin, xMax, yMax)) {
											Index.Close();
										}
										else {
											// no points in file are within grid area...go to next file
											Index.Close();
											continue;
										}
									}
								}

								ldat.Open(m_DataFile[i]);

								if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
									if (ldat.GetFileFormat() == LASDATA) {		// && !m_UseGround if you need min/max Z
										// compare the extent with the grid area...if they overlap, read each point and test
#ifdef USE_LASLIB
										if (!RectanglesIntersect(ldat.lasreader->get_min_x(), ldat.lasreader->get_min_y(), ldat.lasreader->get_max_x(), ldat.lasreader->get_max_y(), xMin, yMin, xMax, yMax)) {
											ldat.Close();
											continue;
										}
#else
										if (ldat.m_HaveLASZIP_DLL) {
											if (!RectanglesIntersect(ldat.lasdll_header->min_x, ldat.lasdll_header->min_y, ldat.lasdll_header->max_x, ldat.lasdll_header->max_y, xMin, yMin, xMax, yMax)) {
												ldat.Close();
												continue;
											}
										}
										else {
											if (!RectanglesIntersect(ldat.m_LASFile.Header.MinX, ldat.m_LASFile.Header.MinY, ldat.m_LASFile.Header.MaxX, ldat.m_LASFile.Header.MaxY, xMin, yMin, xMax, yMax)) {
												ldat.Close();
												continue;
											}
										}
#endif
									}
								}

								m_csStatus.Format("   Reading data from %s (file %i of %i)", (LPCSTR) m_DataFile[i], i + 1, m_DataFileCount);
								LTKCL_PrintVerboseStatus(m_csStatus);

								// @#$%^& using index files slows things down by a factor of 3!!
/*
								// if using a custom extent, try to use index to read points
								if (m_UserGrid || m_UserGridXY || m_ForceAlignment || m_ForceExtent) {
									if (Index.Open(m_DataFile[i])) {
										UseIndex = TRUE;
									}
								}

								// read points using the index
								while (TRUE) {
									if (UseIndex) {
										if ((offset = Index.JumpToNextPointInRange(xMin, yMin, xMax, yMax)) >= 0) {
											ldat.SetPosition(offset, SEEK_SET);
											ldat.ReadNextRecord(&pt);
										}
										else
											break;
									}
									else {
										if (!ldat.ReadNextRecord(&pt))
											break;
									}
*/
								// check for COPC format and register extent
								if (ldat.IsCOPC()) {
									uint64_t chunkCount;

									ldat.RegisterCOPCExtent(xMin, yMin, xMax, yMax);
									
									// see if there are points in the sample...if none go to next file
									chunkCount = ldat.QueryForCOPCChunks();

									if (chunkCount <= 0) {
										ldat.Close();
										continue;
									}
								}


								// read and test each point
								while (TRUE) {		// && !m_UseGround if you need min/max Z
									if (ldat.IsCOPC()) {
										if (!ldat.ReadNextCOPCRecord(&pt))
											break;
									}
									else {
										if (!ldat.ReadNextRecord(&pt))
											break;
									}

									if (ldat.LastPointIsWithheld())
										continue;

									if (ldat.LastPointIsOverlap() && m_SkipOverlapPoints)
										continue;

									if (m_UseFirstReturnsForAllMetrics && pt.ReturnNumber != 1) {
										continue;
									}

									// test to see if point is in grid extent
									if (pt.X < xMin || pt.X > xMax || pt.Y < yMin || pt.Y > yMax)
										continue;

									// moved test for classification before 1st returns are counted and elevation is normalized
									// code was before col and row are calculated
									// 10/20/2009
									// test for specific LAS classification values
									if (m_IncludeSpecificClasses && ldat.GetFileFormat() == LASDATA && m_NumberOfClassCodes > 0) {
#ifdef USE_LASLIB
										if (ldat.lasreader->point.classification > 0 && ldat.lasreader->point.classification < NUMBER_OF_CLASS_CODES) {
											if (m_ClassCodes[ldat.lasreader->point.classification] == 0)
												continue;
										}
#else
										if (ldat.m_HaveLASZIP_DLL) {
											if (ldat.lasdll_point->classification > 0 && ldat.lasdll_point->classification < NUMBER_OF_CLASS_CODES) {
												if (m_ClassCodes[ldat.lasdll_point->classification] == 0) {
													continue;
												}
											}
										}
										else {
											if (ldat.m_LASFile.PointRecord.Classification > 0 && ldat.m_LASFile.PointRecord.Classification < NUMBER_OF_CLASS_CODES) {
												if (m_ClassCodes[ldat.m_LASFile.PointRecord.Classification] == 0)
													continue;
											}
										}
#endif
									}

									// count first returns
									if (pt.ReturnNumber == 1)
										FirstReturns++;

									// adjust for ground surface
									if (m_UseGround) {
										// get elevation from first model covering the XY point
										groundelev = (float)m_Terrain.InterpolateElev(pt.X, pt.Y);
										//										groundelev = InterpolateGroundElevation(pt.X, pt.Y);

																				// normalize return elevation using ground surface elevation
										if (groundelev >= 0.0)
											pt.Elevation = pt.Elevation - groundelev;

										//										if (pt.Elevation < m_HeightBreak)
										//											continue;
									}

									// adjust for canopy surface...not used
//									if (m_UseCanopy && m_CanopyDTM.IsValid()) {
//										canopyelev = m_CanopyDTM.InterpolateElev(pt.X, pt.Y);
//										if (canopyelev >= 0.0) {
//											if (pt.Elevation < (canopyelev - m_CanopyHeightOffset)) {
//												continue;
//											}
//										}
//									}

									// test for outliers
									if (m_EliminateOutliers) {
										if (pt.Elevation < m_OutlierMin || pt.Elevation > m_OutlierMax) {
											continue;
										}
									}

									//									// constrain elevations to be on ground or above
									//									if (pt.Elevation < 0.0)
									//										pt.Elevation = 0.0;

									col = (int)((pt.X - xMin + m_CellSize / 2.0) / m_CellSize);
									row = (int)((pt.Y - yMin + m_CellSize / 2.0) / m_CellSize);
									col = min(cols - 1, col);
									row = min(rows - 1, row);
									col = max(0, col);
									row = max(0, row);

									// old method uses LL corner of cell
//									col = (int) ((pt.X - xMin) / m_CellSize);
//									row = (int) ((pt.Y - yMin) / m_CellSize);

									// compute cell number to give 0 in upper left cell, increasing across the row
									Points[PointCount].CellNumber = ((rows - 1) - row) * cols + col;

									Points[PointCount].ReturnNumber = pt.ReturnNumber;

									// always need elevation to do cover/density calculation...
									// may be duplicated in Value but having Value simplifies the calculation logic
									Points[PointCount].Elevation = pt.Elevation;

									// compute cell number to give 0 in LL cell, increasing up column
//									Points[PointCount].CellNumber = col * rows + row;
									if (m_StatParameter == ELEVATION_VALUE)
										Points[PointCount].Value = pt.Elevation;
									else {
#ifdef USE_LASLIB
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												Points[PointCount].Value = ldat.lasreader->point.rgb[0];
											else if (m_RGBColor == RGB_GREEN)
												Points[PointCount].Value = ldat.lasreader->point.rgb[1];
											else if (m_RGBColor == RGB_BLUE)
												Points[PointCount].Value = ldat.lasreader->point.rgb[2];
										}
										else
											if (ldat.GetFileFormat() == LASDATA)
												Points[PointCount].Value = ldat.lasreader->point.intensity;
											else
												Points[PointCount].Value = pt.Intensity;
#else
										if (ldat.m_HaveLASZIP_DLL) {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													Points[PointCount].Value = ldat.lasdll_point->rgb[0];
												else if (m_RGBColor == RGB_GREEN)
													Points[PointCount].Value = ldat.lasdll_point->rgb[1];
												else if (m_RGBColor == RGB_BLUE)
													Points[PointCount].Value = ldat.lasdll_point->rgb[2];
												else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
													Points[PointCount].Value = ldat.lasdll_point->rgb[3];
											}
											else {
												if (ldat.GetFileFormat() == LASDATA)
													Points[PointCount].Value = ldat.lasdll_point->intensity;
												else
													Points[PointCount].Value = pt.Intensity;
											}
										}
										else {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													Points[PointCount].Value = ldat.m_LASFile.PointRecord.Red;
												else if (m_RGBColor == RGB_GREEN)
													Points[PointCount].Value = ldat.m_LASFile.PointRecord.Green;
												else if (m_RGBColor == RGB_BLUE)
													Points[PointCount].Value = ldat.m_LASFile.PointRecord.Blue;
												else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
													Points[PointCount].Value = ldat.m_LASFile.PointRecord.NIR;
											}
											else
												Points[PointCount].Value = pt.Intensity;
										}
#endif
									}

									PointCount++;
								}

								// close index file (if used)
								if (UseIndex)
									Index.Close();

								ldat.Close();
							}

							if (PointCount) {
								// report total number of first returns or message indicating all returns are used
								if (m_UseFirstReturnsForAllMetrics)
									m_csStatus.Format("   Total first returns used for all metrics: %I64i points\n", FirstReturns);
								else
									m_csStatus.Format("   Total returns used for all metrics: %I64i points\n", PointCount);

								//								else if (m_UseFirstReturnsForDensity)
								//									m_csStatus.Format("   Total first returns used for cover calculation: %i points\n", FirstReturns);
								//								else
								//									m_csStatus.Format("   Using all returns for cover calculation\n");

								LTKCL_PrintStatus(m_csStatus);

								// variables for stats
								int GridPointCount;
								double GridMin;
								double GridMax;
								double GridMean;
								double GridQuadraticMean;
								double GridCubicMean;
								double GridMode;
								double GridStdDev;
								double GridVariance;
								double GridCV;
								double GridSkewness;
								double GridKurtosis;
								double GridAAD;
								double GridFirstDensity;
								double GridFirstDensityMean;
								double GridFirstDensityMode;
								double GridAllFirstDensity;
								double GridAllFirstDensityMean;
								double GridAllFirstDensityMode;
								double GridAllDensity;
								double GridAllDensityMean;
								double GridAllDensityMode;
								double GridIQ;
								double GridL1;
								double GridL2;
								double GridL3;
								double GridL4;
								double GridPA;
								int LCount;

								// l-moments related
								double CL1;
								double CL2;
								double CL3;
								double CR1;
								double CR2;
								double CR3;
								double C1;
								double C2;
								double C3;
								double C4;

								// return counts
								int ReturnCounts[10];

								vbl_array_2d<int> OutlierID(1, 1, 0);

#ifdef INCLUDE_OUTLIER_IMAGE_CODE
								// resize arrays as needed depending on switches
								if (m_CreateOutlierImage)
									OutlierID.resize(rows, cols);
#endif	// INCLUDE_OUTLIER_IMAGE_CODE

								// set up arrays for raster layers
								vbl_array_2d<int> PtCountGrid(1, 1, 0);
								vbl_array_2d<int> PtCountTotalGrid(1, 1, 0);
								vbl_array_2d<int> PtCountAboveGrid(1, 1, 0);
								vbl_array_2d<float> MinGrid(1, 1, 0);
								vbl_array_2d<float> MaxGrid(1, 1, 0);
								vbl_array_2d<float> MeanGrid(1, 1, 0);
								vbl_array_2d<float> ModeGrid(1, 1, 0);
								vbl_array_2d<float> StdDevGrid(1, 1, 0);
								vbl_array_2d<float> VarianceGrid(1, 1, 0);
								vbl_array_2d<float> CVGrid(1, 1, 0);
								vbl_array_2d<float> SkewnessGrid(1, 1, 0);
								vbl_array_2d<float> KurtosisGrid(1, 1, 0);
								vbl_array_2d<float> AADGrid(1, 1, 0);
								vbl_array_2d<float> P01Grid(1, 1, 0);
								vbl_array_2d<float> P05Grid(1, 1, 0);
								vbl_array_2d<float> P10Grid(1, 1, 0);
								vbl_array_2d<float> P20Grid(1, 1, 0);
								vbl_array_2d<float> P25Grid(1, 1, 0);
								vbl_array_2d<float> P30Grid(1, 1, 0);
								vbl_array_2d<float> P40Grid(1, 1, 0);
								vbl_array_2d<float> P50Grid(1, 1, 0);
								vbl_array_2d<float> P60Grid(1, 1, 0);
								vbl_array_2d<float> P70Grid(1, 1, 0);
								vbl_array_2d<float> P75Grid(1, 1, 0);
								vbl_array_2d<float> P80Grid(1, 1, 0);
								vbl_array_2d<float> P90Grid(1, 1, 0);
								vbl_array_2d<float> P95Grid(1, 1, 0);
								vbl_array_2d<float> P99Grid(1, 1, 0);
								vbl_array_2d<float> IQGrid(1, 1, 0);
								vbl_array_2d<float> p90m10Grid(1, 1, 0);
								vbl_array_2d<float> p95m05Grid(1, 1, 0);
								vbl_array_2d<float> DensityGrid(1, 1, 0);
								vbl_array_2d<float> DensityGridMean(1, 1, 0);
								vbl_array_2d<float> DensityGridMode(1, 1, 0);
								vbl_array_2d<float> PtDensity(1, 1, 0);

								vbl_array_2d<int> R1Count(1, 1, 0);
								vbl_array_2d<int> R2Count(1, 1, 0);
								vbl_array_2d<int> R3Count(1, 1, 0);
								vbl_array_2d<int> R4Count(1, 1, 0);
								vbl_array_2d<int> R5Count(1, 1, 0);
								vbl_array_2d<int> R6Count(1, 1, 0);
								vbl_array_2d<int> R7Count(1, 1, 0);
								vbl_array_2d<int> R8Count(1, 1, 0);
								vbl_array_2d<int> R9Count(1, 1, 0);
								vbl_array_2d<int> ROtherCount(1, 1, 0);
								vbl_array_2d<float> AllCover(1, 1, 0);
								vbl_array_2d<float> AllFirstCover(1, 1, 0);
								vbl_array_2d<int> AllCountCover(1, 1, 0);
								vbl_array_2d<float> AllAboveMean(1, 1, 0);
								vbl_array_2d<float> AllAboveMode(1, 1, 0);
								vbl_array_2d<float> AllFirstAboveMean(1, 1, 0);
								vbl_array_2d<float> AllFirstAboveMode(1, 1, 0);
								vbl_array_2d<int> FirstAboveMeanCount(1, 1, 0);
								vbl_array_2d<int> FirstAboveModeCount(1, 1, 0);
								vbl_array_2d<int> AllAboveMeanCount(1, 1, 0);
								vbl_array_2d<int> AllAboveModeCount(1, 1, 0);
								vbl_array_2d<int> TotalFirstCount(1, 1, 0);
								vbl_array_2d<int> TotalAllCount(1, 1, 0);

								// resize arrays for requested parameters
								if (m_Create_PtCountGrid)
									PtCountGrid.resize(rows, cols);
								if (m_Create_PtCountTotalGrid)
									PtCountTotalGrid.resize(rows, cols);
								if (m_Create_PtCountAboveGrid)
									PtCountAboveGrid.resize(rows, cols);
								if (m_Create_MinGrid)
									MinGrid.resize(rows, cols);
								if (m_Create_MaxGrid)
									MaxGrid.resize(rows, cols);
								if (m_Create_MeanGrid)
									MeanGrid.resize(rows, cols);
								if (m_Create_ModeGrid)
									ModeGrid.resize(rows, cols);
								if (m_Create_StdDevGrid)
									StdDevGrid.resize(rows, cols);
								if (m_Create_VarianceGrid)
									VarianceGrid.resize(rows, cols);
								if (m_Create_CVGrid)
									CVGrid.resize(rows, cols);
								if (m_Create_SkewnessGrid)
									SkewnessGrid.resize(rows, cols);
								if (m_Create_KurtosisGrid)
									KurtosisGrid.resize(rows, cols);
								if (m_Create_AADGrid)
									AADGrid.resize(rows, cols);
								if (m_Create_P01Grid)
									P01Grid.resize(rows, cols);
								if (m_Create_P05Grid)
									P05Grid.resize(rows, cols);
								if (m_Create_P10Grid)
									P10Grid.resize(rows, cols);
								if (m_Create_P20Grid)
									P20Grid.resize(rows, cols);
								if (m_Create_P25Grid)
									P25Grid.resize(rows, cols);
								if (m_Create_P30Grid)
									P30Grid.resize(rows, cols);
								if (m_Create_P40Grid)
									P40Grid.resize(rows, cols);
								if (m_Create_P50Grid)
									P50Grid.resize(rows, cols);
								if (m_Create_P60Grid)
									P60Grid.resize(rows, cols);
								if (m_Create_P70Grid)
									P70Grid.resize(rows, cols);
								if (m_Create_P75Grid)
									P75Grid.resize(rows, cols);
								if (m_Create_P80Grid)
									P80Grid.resize(rows, cols);
								if (m_Create_P90Grid)
									P90Grid.resize(rows, cols);
								if (m_Create_P95Grid)
									P95Grid.resize(rows, cols);
								if (m_Create_P99Grid)
									P99Grid.resize(rows, cols);
								if (m_Create_IQGrid)
									IQGrid.resize(rows, cols);
								if (m_Create_90m10Grid)
									p90m10Grid.resize(rows, cols);
								if (m_Create_95m05Grid)
									p95m05Grid.resize(rows, cols);
								if (m_Create_DensityGrid)
									DensityGrid.resize(rows, cols);
								if (m_Create_DensityMeanGrid)
									DensityGridMean.resize(rows, cols);
								if (m_Create_DensityModeGrid)
									DensityGridMode.resize(rows, cols);
								if (m_Create_DensityCell)
									PtDensity.resize(rows, cols);
								if (m_Create_R1Count)
									R1Count.resize(rows, cols);
								if (m_Create_R2Count)
									R2Count.resize(rows, cols);
								if (m_Create_R3Count)
									R3Count.resize(rows, cols);
								if (m_Create_R4Count)
									R4Count.resize(rows, cols);
								if (m_Create_R5Count)
									R5Count.resize(rows, cols);
								if (m_Create_R6Count)
									R6Count.resize(rows, cols);
								if (m_Create_R7Count)
									R7Count.resize(rows, cols);
								if (m_Create_R8Count)
									R8Count.resize(rows, cols);
								if (m_Create_R9Count)
									R9Count.resize(rows, cols);
								if (m_Create_ROtherCount)
									ROtherCount.resize(rows, cols);
								if (m_Create_AllCover)
									AllCover.resize(rows, cols);
								if (m_Create_AllFirstCover)
									AllFirstCover.resize(rows, cols);
								if (m_Create_AllCountCover)
									AllCountCover.resize(rows, cols);
								if (m_Create_AllAboveMean)
									AllAboveMean.resize(rows, cols);
								if (m_Create_AllAboveMode)
									AllAboveMode.resize(rows, cols);
								if (m_Create_AllFirstAboveMean)
									AllFirstAboveMean.resize(rows, cols);
								if (m_Create_AllFirstAboveMode)
									AllFirstAboveMode.resize(rows, cols);
								if (m_Create_FirstAboveMeanCount)
									FirstAboveMeanCount.resize(rows, cols);
								if (m_Create_FirstAboveModeCount)
									FirstAboveModeCount.resize(rows, cols);
								if (m_Create_AllAboveMeanCount)
									AllAboveMeanCount.resize(rows, cols);
								if (m_Create_AllAboveModeCount)
									AllAboveModeCount.resize(rows, cols);
								if (m_Create_TotalFirstCount)
									TotalFirstCount.resize(rows, cols);
								if (m_Create_TotalAllCount)
									TotalAllCount.resize(rows, cols);

								// *************************************************
																// 10/16/2009
																// changed logic to compute both intensity and elevation metrics on a single pass through GridMetrics
																// previously you had to run GridMetrics twice to get all the metrics...SLOWER THAN NECESSARY

																// declare variables used in loop...moved 10/15/2009
								CFileSpec fs;
								CFileSpec Fuelfs;
								CFileSpec Stratafs;
								CString Ft;
								FILE* OutputFile;
								FILE* FuelOutputFile;
								FILE* StrataOutputFile;
								int CurrentCellNumber;
								int CurrentPointNumber = 0;
								//double ValueSum;
								//double ValueSumSq;
								double ElevSumSquare, ElevSumCube;
								double oldM, oldS, newM, newS;
								int FirstReturnsAboveThreshold;
								int FirstReturnsTotal;
								int FirstReturnsAboveMean;
								int FirstReturnsAboveMode;
								int FirstReturnsTotalMeanMode;
								int AllReturnsAboveThreshold;
								int AllReturnsTotal;
								int AllReturnsAboveMean;
								int AllReturnsAboveMode;
								int AllReturnsTotalMeanMode;
								int CellCount;
								int MaxCellCount;
								int FirstCellPoint = 0;
								int MinPtIndex;
								int IntensityMinPtIndex;
								int IntensityCellCount;
								int MaxCount;

								// variables for fuel models
								double CanopyFuelWeight;
								double CanopyBulkDensity;
								double CanopyBaseHeight;
								double CanopyHeight;

								// new variables 1/24/2011
								float* MedianDiffValueList = NULL;
								float* ModeDiffValueList = NULL;
								double CanopyReliefRatio, GridMadMedian, GridMadMode;
								int KDE_ModeCount;
								double KDE_MaxMode, KDE_MinMode, KDE_ModeRange;
								int ElevStrataCount[MAX_NUMBER_OF_STRATA];
								int ElevStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
								double ElevStrataMean[MAX_NUMBER_OF_STRATA];
								double ElevStrataMin[MAX_NUMBER_OF_STRATA];
								double ElevStrataMax[MAX_NUMBER_OF_STRATA];
								double ElevStrataMedian[MAX_NUMBER_OF_STRATA];
								double ElevStrataMode[MAX_NUMBER_OF_STRATA];
								double ElevStrataSkewness[MAX_NUMBER_OF_STRATA];
								double ElevStrataKurtosis[MAX_NUMBER_OF_STRATA];
								double ElevStrataVariance[MAX_NUMBER_OF_STRATA];
								double ElevStrataM2[MAX_NUMBER_OF_STRATA];		// used to compute variance
								double ElevStrataM3[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
								double ElevStrataM4[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis

								int IntStrataCount[MAX_NUMBER_OF_STRATA];
								int IntStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
								double IntStrataMean[MAX_NUMBER_OF_STRATA];
								double IntStrataMin[MAX_NUMBER_OF_STRATA];
								double IntStrataMax[MAX_NUMBER_OF_STRATA];
								double IntStrataMedian[MAX_NUMBER_OF_STRATA];
								double IntStrataMode[MAX_NUMBER_OF_STRATA];
								double IntStrataSkewness[MAX_NUMBER_OF_STRATA];
								double IntStrataKurtosis[MAX_NUMBER_OF_STRATA];
								double IntStrataVariance[MAX_NUMBER_OF_STRATA];
								double IntStrataM2[MAX_NUMBER_OF_STRATA];		// used to compute variance
								double IntStrataM3[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
								double IntStrataM4[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
								double delta;
								double delta_n;
								double delta_n2;
								double term1;
								int n1;
								double BW;
								// end of new variables

								double x, y;

								/* shouldn't need to initialize these variables in this part of the code...they are initialized for each cell 11/16/2011
																// initialize strata counts and std dev (will be used to accumulate values)
																for (k = 0; k < MAX_NUMBER_OF_STRATA; k ++) {
																	ElevStrataCount[k] = 0;
																	ElevStrataVariance[k] = 0.0;
																	ElevStrataMean[k] = 0.0;
																	ElevStrataMin[k] = DBL_MAX;
																	ElevStrataMax[k] = -DBL_MAX;
																	ElevStrataMedian[k] = 0.0;
																	ElevStrataMode[k] = 0.0;
																	ElevStrataSkewness[k] = 0.0;
																	ElevStrataKurtosis[k] = 0.0;
																	ElevStrataM2[k] = 0.0;
																	ElevStrataM3[k] = 0.0;
																	ElevStrataM4[k] = 0.0;
																	for (j = 0; j < 10; j ++)
																		ElevStrataCountReturn[k][j] = 0;

																	IntStrataCount[k] = 0;
																	IntStrataVariance[k] = 0.0;
																	IntStrataMean[k] = 0.0;
																	IntStrataMin[k] = DBL_MAX;
																	IntStrataMax[k] = -DBL_MAX;
																	IntStrataMedian[k] = 0.0;
																	IntStrataMode[k] = 0.0;
																	IntStrataSkewness[k] = 0.0;
																	IntStrataKurtosis[k] = 0.0;
																	IntStrataM2[k] = 0.0;
																	IntStrataM3[k] = 0.0;
																	IntStrataM4[k] = 0.0;
																	for (j = 0; j < 10; j ++)
																		IntStrataCountReturn[k][j] = 0;
																}
								*/ // end 11/16/2011

								// set up loop to compute the intensity metrics, then the elevation metrics
								while (m_StatParameter < METRICS_DONE) {
									// if we are doing the second pass (computing elevation metrics, swap elevation values into "Value"
									// "Value" will have intensity data the first time through
									if (m_StatParameter == ELEVATION_VALUE) {
										for (i = 0; i < PointCount; i++)
											Points[i].Value = Points[i].Elevation;
									}

									// initialize arrays
									OutlierID.fill(0);
									PtCountGrid.fill(-1);
									PtCountTotalGrid.fill(-1);
									PtCountAboveGrid.fill(-1);
									MinGrid.fill(-1.0);
									MaxGrid.fill(-1.0);
									MeanGrid.fill(-1.0);
									ModeGrid.fill(-1.0);
									StdDevGrid.fill(-1.0);
									VarianceGrid.fill(-1.0);
									CVGrid.fill(-1.0);
									SkewnessGrid.fill(-1.0);
									KurtosisGrid.fill(-1.0);
									AADGrid.fill(-1.0);
									P05Grid.fill(-1.0);
									P10Grid.fill(-1.0);
									P20Grid.fill(-1.0);
									P25Grid.fill(-1.0);
									P30Grid.fill(-1.0);
									P40Grid.fill(-1.0);
									P50Grid.fill(-1.0);
									P60Grid.fill(-1.0);
									P70Grid.fill(-1.0);
									P75Grid.fill(-1.0);
									P80Grid.fill(-1.0);
									P90Grid.fill(-1.0);
									P95Grid.fill(-1.0);
									IQGrid.fill(-1.0);
									p90m10Grid.fill(-1.0);
									DensityGrid.fill(-1.0);
									DensityGridMean.fill(-1.0);
									DensityGridMode.fill(-1.0);
									PtDensity.fill(-1.0);

									// build output file name
									fs.SetFullSpec(m_OutputFile);
									Ft = fs.FileTitle();
									if (m_UseFirstReturnsForAllMetrics)
										Ft = Ft + _T("_first_returns");
									//									else if (m_UseFirstReturnsForDensity)
									//										Ft = Ft + _T("_first_returns_density_only");
									else
										Ft = Ft + _T("_all_returns");

									if (m_StatParameter == ELEVATION_VALUE)
										Ft = Ft + _T("_elevation_stats");
									else {
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												Ft = Ft + _T("_RED_stats");
											else if (m_RGBColor == RGB_GREEN)
												Ft = Ft + _T("_GREEN_stats");
											else if (m_RGBColor == RGB_BLUE)
												Ft = Ft + _T("_BLUE_stats");
										}
										else
											Ft = Ft + _T("_intensity_stats");
									}
									fs.SetTitle(Ft);
									fs.SetExt(".csv");

									// open output file and write ouput header
									OutputFile = NULL;
									if (!m_NoCSVFile)
										OutputFile = fopen(fs.GetFullSpec(), "wt");

									if (OutputFile) {
										if (m_StatParameter == ELEVATION_VALUE) {
											// headers for elevation metrics
											if (m_MinHeightForMetrics != -99999.0) {
												fprintf(OutputFile, "row,col,center X,center Y,Total return count above %.2lf", m_MinHeightForMetrics);
											}
											else {
												fprintf(OutputFile, "row,col,center X,center Y,Total return count");
											}

											fprintf(OutputFile, ",Elev minimum,Elev maximum,Elev mean,Elev mode,Elev stddev,Elev variance,Elev CV,Elev IQ,Elev skewness,Elev kurtosis,Elev AAD,Elev L1,Elev L2,Elev L3,Elev L4,Elev L CV,Elev L skewness,Elev L kurtosis,Elev P01,Elev P05,Elev P10,Elev P20,Elev P25,Elev P30,Elev P40,Elev P50,Elev P60,Elev P70,Elev P75,Elev P80,Elev P90,Elev P95,Elev P99");

											if (m_MinHeightForMetrics != -99999.0) {
												fprintf(OutputFile, ",Return 1 count above %.2lf,Return 2 count above %.2lf,Return 3 count above %.2lf,Return 4 count above %.2lf,Return 5 count above %.2lf,Return 6 count above %.2lf,Return 7 count above %.2lf,Return 8 count above %.2lf,Return 9 count above %.2lf,Other return count above %.2lf", m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics, m_MinHeightForMetrics);
											}
											else {
												fprintf(OutputFile, ",Return 1 count,Return 2 count,Return 3 count,Return 4 count,Return 5 count,Return 6 count,Return 7 count,Return 8 count,Return 9 count,Other return count");
											}

											fprintf(OutputFile, ",Percentage first returns above %.2lf", m_HeightBreak);
											fprintf(OutputFile, ",Percentage all returns above %.2lf", m_HeightBreak);
											fprintf(OutputFile, ",(All returns above %.2lf) / (Total first returns) * 100", m_HeightBreak);
											fprintf(OutputFile, ",First returns above %.2lf", m_HeightBreak);
											fprintf(OutputFile, ",All returns above %.2lf", m_HeightBreak);
											fprintf(OutputFile, ",Percentage first returns above mean");
											fprintf(OutputFile, ",Percentage first returns above mode");
											fprintf(OutputFile, ",Percentage all returns above mean");
											fprintf(OutputFile, ",Percentage all returns above mode");
											fprintf(OutputFile, ",(All returns above mean) / (Total first returns) * 100");
											fprintf(OutputFile, ",(All returns above mode) / (Total first returns) * 100");
											fprintf(OutputFile, ",First returns above mean");
											fprintf(OutputFile, ",First returns above mode");
											fprintf(OutputFile, ",All returns above mean");
											fprintf(OutputFile, ",All returns above mode");
											fprintf(OutputFile, ",Total first returns");
											fprintf(OutputFile, ",Total all returns");

											fprintf(OutputFile, ",Elev MAD median,Elev MAD mode,Canopy relief ratio,Elev quadratic mean,Elev cubic mean,Profile area");

											if (m_DoKDEStats)
												fprintf(OutputFile, ",KDE elev modes,KDE elev min mode,KDE elev max mode,KDE elev mode range");

											if (m_UseGlobalIdentifier) {
												fprintf(OutputFile, ",Identifier\n");
											}
											else {
												fprintf(OutputFile, "\n");
											}
										}
										else {
											// headers for intensity metrics
											if (m_MinHeightForMetrics != -99999.0)
												fprintf(OutputFile, "row,col,center X,center Y,Total return count above %.2lf", m_MinHeightForMetrics);
											else
												fprintf(OutputFile, "row,col,center X,center Y,Total return count");

											fprintf(OutputFile, ",Int minimum,Int maximum,Int mean,Int mode,Int stddev,Int variance,Int CV,Int IQ,Int skewness,Int kurtosis,Int AAD,Int L1,Int L2,Int L3,Int L4,Int L CV,Int L skewness,Int L kurtosis,Int P01,Int P05,Int P10,Int P20,Int P25,Int P30,Int P40,Int P50,Int P60,Int P70,Int P75,Int P80,Int P90,Int P95,Int P99");

											if (m_UseGlobalIdentifier) {
												fprintf(OutputFile, ",Identifier\n");
											}
											else {
												fprintf(OutputFile, "\n");
											}
										}
									}

									// set up for fuel model results
									Fuelfs.SetFullSpec(m_OutputFile);
									Ft = Fuelfs.FileTitle();
									FuelOutputFile = NULL;
									if (m_ApplyFuelModels && m_StatParameter == ELEVATION_VALUE) {
										Ft = Ft + _T("_fuel_parameters");
										Fuelfs.SetTitle(Ft);
										Fuelfs.SetExt(".csv");

										FuelOutputFile = fopen(Fuelfs.GetFullSpec(), "wt");
										fprintf(FuelOutputFile, "row,col,canopy fuel weight,canopy bulk density,canopy base height,canopy height,center x,center y");

										if (m_UseGlobalIdentifier) {
											fprintf(FuelOutputFile, ",Identifier\n");
										}
										else {
											fprintf(FuelOutputFile, "\n");
										}
									}

									// set up file for strata results
									Stratafs.SetFullSpec(m_OutputFile);
									Ft = Stratafs.FileTitle();
									StrataOutputFile = NULL;
									if ((m_DoHeightStrata || m_DoHeightStrataIntensity) && m_StatParameter == INTENSITY_VALUE) {
										if (m_UseFirstReturnsForAllMetrics)
											Ft = Ft + _T("_first_returns");
										else
											Ft = Ft + _T("_all_returns");

										Ft = Ft + _T("_strata_stats");
										Stratafs.SetTitle(Ft);
										Stratafs.SetExt(".csv");

										StrataOutputFile = fopen(Stratafs.GetFullSpec(), "wt");
										fprintf(StrataOutputFile, "row,col");

										if (m_DoHeightStrata) {
											// print column labels...don't use last strata as it was "added" to provide upper bound
											// first strata
											fprintf(StrataOutputFile, ",Elev strata (below %.2lf) total return count,Elev strata (below %.2lf) return proportion,Elev strata (below %.2lf) min,Elev strata (below %.2lf) max,Elev strata (below %.2lf) mean,Elev strata (below %.2lf) mode,Elev strata (below %.2lf) median,Elev strata (below %.2lf) stddev,Elev strata (below %.2lf) CV,Elev strata (below %.2lf) skewness,Elev strata (below %.2lf) kurtosis",
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0],
												m_HeightStrata[0]);

											for (k = 1; k < m_HeightStrataCount - 1; k++) {
												fprintf(StrataOutputFile, ",Elev strata (%.2lf to %.2lf) total return count,Elev strata (%.2lf to %.2lf) return proportion,Elev strata (%.2lf to %.2lf) min,Elev strata (%.2lf to %.2lf) max,Elev strata (%.2lf to %.2lf) mean,Elev strata (%.2lf to %.2lf) mode,Elev strata (%.2lf to %.2lf) median,Elev strata (%.2lf to %.2lf) stddev,Elev strata (%.2lf to %.2lf) CV,Elev strata (%.2lf to %.2lf) skewness,Elev strata (%.2lf to %.2lf) kurtosis",
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k],
													m_HeightStrata[k - 1], m_HeightStrata[k]);
											}

											// last strata
											fprintf(StrataOutputFile, ",Elev strata (above %.2lf) total return count,Elev strata (above %.2lf) return proportion,Elev strata (above %.2lf) min,Elev strata (above %.2lf) max,Elev strata (above %.2lf) mean,Elev strata (above %.2lf) mode,Elev strata (above %.2lf) median,Elev strata (above %.2lf) stddev,Elev strata (above %.2lf) CV,Elev strata (above %.2lf) skewness,Elev strata (above %.2lf) kurtosis",
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2],
												m_HeightStrata[m_HeightStrataCount - 2]);
										}

										if (m_DoHeightStrataIntensity) {
											// print column labels...don't use last strata as it was "added" to provide upper bound
											// first strata
											fprintf(StrataOutputFile, ",Int strata (below %.2lf) total return count,Int strata (below %.2lf) return proportion,Int strata (below %.2lf) min,Int strata (below %.2lf) max,Int strata (below %.2lf) mean,Int strata (below %.2lf) mode,Int strata (below %.2lf) median,Int strata (below %.2lf) stddev,Int strata (below %.2lf) CV,Int strata (below %.2lf) skewness,Int strata (below %.2lf) kurtosis",
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0],
												m_HeightStrataIntensity[0]);

											for (k = 1; k < m_HeightStrataIntensityCount - 1; k++) {
												fprintf(StrataOutputFile, ",Int strata (%.2lf to %.2lf) total return count,Int strata (%.2lf to %.2lf) return proportion,Int strata (%.2lf to %.2lf) min,Int strata (%.2lf to %.2lf) max,Int strata (%.2lf to %.2lf) mean,Int strata (%.2lf to %.2lf) mode,Int strata (%.2lf to %.2lf) median,Int strata (%.2lf to %.2lf) stddev,Int strata (%.2lf to %.2lf) CV,Int strata (%.2lf to %.2lf) skewness,Int strata (%.2lf to %.2lf) kurtosis",
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k],
													m_HeightStrataIntensity[k - 1], m_HeightStrataIntensity[k]);
											}

											// last strata
											fprintf(StrataOutputFile, ",Int strata (above %.2lf) total return count,Int strata (above %.2lf) return proportion,Int strata (above %.2lf) min,Int strata (above %.2lf) max,Int strata (above %.2lf) mean,Int strata (above %.2lf) mode,Int strata (above %.2lf) median,Int strata (above %.2lf) stddev,Int strata (above %.2lf) CV,Int strata (above %.2lf) skewness,Int strata (above %.2lf) kurtosis",
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2],
												m_HeightStrataIntensity[m_HeightStrataIntensityCount - 2]);
										}

										if (m_UseGlobalIdentifier) {
											fprintf(StrataOutputFile, ",Identifier\n");
										}
										else {
											fprintf(StrataOutputFile, "\n");
										}
									}

									// sort the list into cells and by values
									if (m_StatParameter == INTENSITY_VALUE) {
										qsort(Points, (size_t)PointCount, sizeof(PointRecord), comparevalueforintensity);
									}
									else {
										qsort(Points, (size_t)PointCount, sizeof(PointRecord), comparevalue);
									}

									// do intitialization
									CellInBuffer = FALSE;
									CellOffsetRows = 0;
									CellOffsetCols = 0;
									CurrentPointNumber = 0;
									FirstCellPoint = 0;

									// added to track actual start and stop row/col output when using distance buffer
									MinRowOutput = rows;
									MaxRowOutput = 0;
									MinColOutput = cols;
									MaxColOutput = 0;

									// figure out the offset needed to correctly number cells when using a buffer
	//								if (m_AddBufferDistance) {
	//									CellOffsetRows = (int) (m_BufferDistance / m_CellSize);
	//									CellOffsetCols = (int) (m_BufferDistance / m_CellSize);
	//								}
									if (m_AddBufferDistance || m_AddBufferCells) {
										CellOffsetRows = m_BufferCells;
										CellOffsetCols = m_BufferCells;
									}

									// set output row and column values for full model
									if (!m_AddBufferDistance && !m_AddBufferCells) {
										MinRowOutput = 0;
										MaxRowOutput = rows - 1;
										MinColOutput = 0;
										MaxColOutput = cols - 1;
									}

									// run through the points and find the maximum number of points in a cell...
									// needed to create a list for values for MAD calculations
									if (MedianDiffValueList == NULL && ModeDiffValueList == NULL) {
										MaxCellCount = 0;
										y = yMin + (double)(rows - 1) * m_CellSize;
										for (j = rows - 1; j >= 0; j--) {
											x = xMin;
											for (i = 0; i < cols; i++) {
												CurrentCellNumber = ((rows - 1) - j) * cols + i;
												if (CurrentPointNumber < PointCount) {
													// count points
													CellCount = 0;
													while (Points[CurrentPointNumber].CellNumber == CurrentCellNumber) {
														CellCount++;
														CurrentPointNumber++;

														if (CurrentPointNumber == PointCount)
															break;
													}
													MaxCellCount = max(MaxCellCount, CellCount);
												}
											}
										}

										// allocate memory for lists used for MAD calculations
										MedianDiffValueList = new float[MaxCellCount];
										ModeDiffValueList = new float[MaxCellCount];

										if (!MedianDiffValueList) {
											LTKCL_PrintStatus("Insufficient memory to compute median absolute difference from the median");
										}
										if (!ModeDiffValueList) {
											LTKCL_PrintStatus("Insufficient memory to compute median absolute difference from the mode");
										}
									}

									// step through cells and compute metric for each cell with LIDAR points
									CurrentPointNumber = 0;
									y = yMin + (double)(rows - 1) * m_CellSize;
									for (j = rows - 1; j >= 0; j--) {
										x = xMin;
										for (i = 0; i < cols; i++) {
											// loop to run from LL corner up column then next column
				//								for (i = 0; i < cols; i ++) {
				//									for (j = 0; j < rows; j ++) {
											CurrentCellNumber = ((rows - 1) - j) * cols + i;

											// compute cell number using 0 in LL cell, increasing up column
		//										CurrentCellNumber = i * rows + j;

											// see if there are any more points...if not, we have to write values for the cell and continue
											// processing to make sure we end up with a complete output file
											if (CurrentPointNumber == PointCount) {
												// no points in cell
												GridMean = -9999.0;
												GridQuadraticMean = -9999.0;
												GridCubicMean = -9999.0;
												GridMode = -9999.0;
												GridStdDev = -9999.0;
												GridVariance = -9999.0;
												GridCV = -9999.0;
												GridSkewness = -9999.0;
												GridKurtosis = -9999.0;
												GridAAD = -9999.0;
												GridMadMedian = -9999.0;
												GridMadMode = -9999.0;
												GridIQ = -9999.0;
												GridMin = -9999.0;
												GridMax = -9999.0;
												GridFirstDensity = -9999.0;
												GridFirstDensityMean = -9999.0;
												GridFirstDensityMode = -9999.0;
												GridAllFirstDensity = -9999.0;
												GridAllFirstDensityMean = -9999.0;
												GridAllFirstDensityMode = -9999.0;
												GridAllDensity = -9999.0;
												GridAllDensityMean = -9999.0;
												GridAllDensityMode = -9999.0;
												GridAllFirstDensityMean = -9999.0;
												GridAllFirstDensityMode = -9999.0;
												FirstReturnsAboveThreshold = -9999;
												FirstReturnsTotal = -9999;
												FirstReturnsAboveMean = -9999;
												FirstReturnsAboveMode = -9999;
												FirstReturnsTotalMeanMode = -9999;
												AllReturnsAboveThreshold = -9999;
												AllReturnsTotal = -9999;
												AllReturnsAboveMean = -9999;
												AllReturnsAboveMode = -9999;
												AllReturnsTotalMeanMode = -9999;
												CanopyReliefRatio = -9999.0;
												GridL1 = GridL2 = GridL3 = GridL4 = -9999.0;
												GridPA = -9999.0;
												ElevSumSquare = 0.0;
												ElevSumCube = 0.0;
												KDE_ModeCount = -9999;
												KDE_MaxMode = -9999.0;
												KDE_MinMode = -9999.0;
												KDE_ModeRange = -9999.0;

												// set percentiles
												for (k = 0; k <= 20; k++) {
													Percentile[k] = -9999.0;
												}

												// initialize return counts
												for (k = 0; k < 10; k++) {
													ReturnCounts[k] = -9999;		// 1/6/2012 set this to -9999 instead of 0
												}

												GridPointCount = -9999;		// 1/6/2012 set this to -9999 instead of 0

												// have values for current cell...
												// see if cell is in "buffer"
												if (m_AddBufferDistance || m_AddBufferCells) {
													CellInBuffer = FALSE;

													//												if (m_AddBufferDistance) {
													//													if (x - xMin < m_BufferDistance || xMax - x < m_BufferDistance)
													//														CellInBuffer = TRUE;
													//													if (y - yMin < m_BufferDistance || yMax - y < m_BufferDistance)
													//														CellInBuffer = TRUE;
													//												}

													if (m_AddBufferDistance || m_AddBufferCells) {
														if (j > (rows - 1) - m_BufferCells || j < m_BufferCells)
															CellInBuffer = TRUE;

														if (i > (cols - 1) - m_BufferCells || i < m_BufferCells)
															CellInBuffer = TRUE;
													}
												}

												if (OutputFile && !CellInBuffer) {
													if (m_StatParameter == ELEVATION_VALUE) {
														//                   R  C  X     Y     #  min   max   mean  mode  stdv  var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L2/L1 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99  #1 #2 #3 #4 #5 #6 #7 #8 #9 #o %1    %a    %1af  #1 #a %     %     %     %     %     %     #  #  #  #  #  #  mmed  mmod  ccr   qave  cave
														fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
															j - CellOffsetRows,
															i - CellOffsetCols,
															x,
															y,
															GridPointCount,
															GridMin,
															GridMax,
															GridMean,
															GridMode,
															GridStdDev,
															GridVariance,
															GridCV,
															GridIQ,
															GridSkewness,
															GridKurtosis,
															GridAAD,
															GridL1,
															GridL2,
															GridL3,
															GridL4,
															((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
															((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
															((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
															Percentile[0],
															Percentile[1],
															Percentile[2],
															Percentile[4],
															Percentile[5],
															Percentile[6],
															Percentile[8],
															Percentile[10],
															Percentile[12],
															Percentile[14],
															Percentile[15],
															Percentile[16],
															Percentile[18],
															Percentile[19],
															Percentile[20],
															ReturnCounts[0],
															ReturnCounts[1],
															ReturnCounts[2],
															ReturnCounts[3],
															ReturnCounts[4],
															ReturnCounts[5],
															ReturnCounts[6],
															ReturnCounts[7],
															ReturnCounts[8],
															ReturnCounts[9],
															GridFirstDensity,
															GridAllDensity,
															GridAllFirstDensity,
															FirstReturnsAboveThreshold,
															AllReturnsAboveThreshold,
															GridFirstDensityMean,
															GridFirstDensityMode,
															GridAllDensityMean,
															GridAllDensityMode,
															GridAllFirstDensityMean,
															GridAllFirstDensityMode,
															FirstReturnsAboveMean,
															FirstReturnsAboveMode,
															AllReturnsAboveMean,
															AllReturnsAboveMode,
															FirstReturnsTotal,
															AllReturnsTotal,
															GridMadMedian,
															GridMadMode,
															CanopyReliefRatio,
															GridQuadraticMean,
															GridCubicMean,
															GridPA);

														if (m_DoKDEStats)
															fprintf(OutputFile, ",%i,%.4lf,%.4lf,%.4lf", KDE_ModeCount, KDE_MinMode, KDE_MaxMode, KDE_ModeRange);

														if (m_UseGlobalIdentifier) {
															fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
														}
														else {
															fprintf(OutputFile, "\n");
														}
													}
													else {
														//                   R  C  X     Y     #  min   max   ave   mode  stdv  var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L1/L2 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99
														fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
															j - CellOffsetRows,
															i - CellOffsetCols,
															x,
															y,
															GridPointCount,
															GridMin,
															GridMax,
															GridMean,
															GridMode,
															GridStdDev,
															GridVariance,
															GridCV,
															GridIQ,
															GridSkewness,
															GridKurtosis,
															GridAAD,
															GridL1,
															GridL2,
															GridL3,
															GridL4,
															((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
															((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
															((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
															//															GridL2 / GridL1,
															//															GridL3 / GridL2,
															//															GridL4 / GridL2,
															Percentile[0],
															Percentile[1],
															Percentile[2],
															Percentile[4],
															Percentile[5],
															Percentile[6],
															Percentile[8],
															Percentile[10],
															Percentile[12],
															Percentile[14],
															Percentile[15],
															Percentile[16],
															Percentile[18],
															Percentile[19],
															Percentile[20]);

														if (m_UseGlobalIdentifier) {
															fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
														}
														else {
															fprintf(OutputFile, "\n");
														}
													}

													if (m_AddBufferDistance || m_AddBufferCells) {
														MinRowOutput = min(MinRowOutput, j);
														MaxRowOutput = max(MaxRowOutput, j);
														MinColOutput = min(MinColOutput, i);
														MaxColOutput = max(MaxColOutput, i);
													}
												}

												if (m_ApplyFuelModels && !CellInBuffer && m_StatParameter == ELEVATION_VALUE) {
													fprintf(FuelOutputFile, "%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
														j - CellOffsetRows,
														i - CellOffsetCols,
														-9999.0,
														-9999.0,
														-9999.0,
														-9999.0,
														x,
														y);

													if (m_UseGlobalIdentifier) {
														fprintf(FuelOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(FuelOutputFile, "\n");
													}

													if (m_AddBufferDistance || m_AddBufferCells) {
														MinRowOutput = min(MinRowOutput, j);
														MaxRowOutput = max(MaxRowOutput, j);
														MinColOutput = min(MinColOutput, i);
														MaxColOutput = max(MaxColOutput, i);
													}
												}

												if ((m_DoHeightStrata || m_DoHeightStrataIntensity) && !CellInBuffer && m_StatParameter == INTENSITY_VALUE) {
													fprintf(StrataOutputFile, "%i,%i",
														j - CellOffsetRows,
														i - CellOffsetCols);

													if (m_DoHeightStrata) {
														for (k = 0; k < m_HeightStrataCount; k++) {
															fprintf(StrataOutputFile, ",-9999,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0");
														}
													}

													if (m_DoHeightStrataIntensity) {
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															fprintf(StrataOutputFile, ",-9999,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0");
														}
													}

													if (m_UseGlobalIdentifier) {
														fprintf(StrataOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(StrataOutputFile, "\n");
													}
												}

												x += m_CellSize;
												continue;
											}

											// see if there are any points in the cell
											if (Points[CurrentPointNumber].CellNumber > CurrentCellNumber) {
												// no points in cell
												GridMean = -9999.0;
												GridQuadraticMean = -9999.0;
												GridCubicMean = -9999.0;
												GridMode = -9999.0;
												GridStdDev = -9999.0;
												GridVariance = -9999.0;
												GridCV = -9999.0;
												GridSkewness = -9999.0;
												GridKurtosis = -9999.0;
												GridAAD = -9999.0;
												GridMadMedian = -9999.0;
												GridMadMode = -9999.0;
												GridIQ = -9999.0;
												GridMin = -9999.0;
												GridMax = -9999.0;
												GridFirstDensity = -9999.0;
												GridFirstDensityMean = -9999.0;
												GridFirstDensityMode = -9999.0;
												GridAllFirstDensity = -9999.0;
												GridAllFirstDensityMean = -9999.0;
												GridAllFirstDensityMode = -9999.0;
												GridAllDensity = -9999.0;
												GridAllDensityMean = -9999.0;
												GridAllDensityMode = -9999.0;
												GridAllFirstDensityMean = -9999.0;
												GridAllFirstDensityMode = -9999.0;
												FirstReturnsAboveThreshold = -9999;
												FirstReturnsTotal = -9999;
												FirstReturnsAboveMean = -9999;
												FirstReturnsAboveMode = -9999;
												FirstReturnsTotalMeanMode = -9999;
												AllReturnsAboveThreshold = -9999;
												AllReturnsTotal = -9999;
												AllReturnsAboveMean = -9999;
												AllReturnsAboveMode = -9999;
												AllReturnsTotalMeanMode = -9999;
												CanopyReliefRatio = -9999.0;
												GridL1 = GridL2 = GridL3 = GridL4 = -9999.0;
												GridPA = -9999.0;
												KDE_ModeCount = -9999;
												KDE_MaxMode = -9999.0;
												KDE_MinMode = -9999.0;
												KDE_ModeRange = -9999.0;

												// set percentiles
												for (k = 0; k <= 20; k++) {
													Percentile[k] = -9999.0;
												}

												// initialize return counts
												for (k = 0; k < 10; k++) {
													ReturnCounts[k] = -9999;			// 1/6/2012 set this to -9999 instead of 0
												}

												GridPointCount = -9999;			// 1/6/2012 set this to -9999 instead of 0

												// have values for current cell...
												// see if cell is in "buffer"
												if (m_AddBufferDistance || m_AddBufferCells) {
													CellInBuffer = FALSE;

													//												if (m_AddBufferDistance) {
													//													if (x - xMin < m_BufferDistance || xMax - x < m_BufferDistance)
													//														CellInBuffer = TRUE;
													//													if (y - yMin < m_BufferDistance || yMax - y < m_BufferDistance)
													//														CellInBuffer = TRUE;
													//												}

													if (m_AddBufferDistance || m_AddBufferCells) {
														if (j > (rows - 1) - m_BufferCells || j < m_BufferCells)
															CellInBuffer = TRUE;

														if (i > (cols - 1) - m_BufferCells || i < m_BufferCells)
															CellInBuffer = TRUE;
													}
												}

												if (OutputFile && !CellInBuffer) {
													if (m_StatParameter == ELEVATION_VALUE) {
														//                   R  C  X     Y     #  min   max   mean  mode  stddv var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L2/L1 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99  #1 #2 #3 #4 #5 #6 #7 #8 #9 #o
														fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
															j - CellOffsetRows,
															i - CellOffsetCols,
															x,
															y,
															GridPointCount,
															GridMin,
															GridMax,
															GridMean,
															GridMode,
															GridStdDev,
															GridVariance,
															GridCV,
															GridIQ,
															GridSkewness,
															GridKurtosis,
															GridAAD,
															GridL1,
															GridL2,
															GridL3,
															GridL4,
															((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
															((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
															((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
															Percentile[0],
															Percentile[1],
															Percentile[2],
															Percentile[4],
															Percentile[5],
															Percentile[6],
															Percentile[8],
															Percentile[10],
															Percentile[12],
															Percentile[14],
															Percentile[15],
															Percentile[16],
															Percentile[18],
															Percentile[19],
															Percentile[20],
															ReturnCounts[0],
															ReturnCounts[1],
															ReturnCounts[2],
															ReturnCounts[3],
															ReturnCounts[4],
															ReturnCounts[5],
															ReturnCounts[6],
															ReturnCounts[7],
															ReturnCounts[8],
															ReturnCounts[9],
															GridFirstDensity,
															GridAllDensity,
															GridAllFirstDensity,
															FirstReturnsAboveThreshold,
															AllReturnsAboveThreshold,
															GridFirstDensityMean,
															GridFirstDensityMode,
															GridAllDensityMean,
															GridAllDensityMode,
															GridAllFirstDensityMean,
															GridAllFirstDensityMode,
															FirstReturnsAboveMean,
															FirstReturnsAboveMode,
															AllReturnsAboveMean,
															AllReturnsAboveMode,
															FirstReturnsTotal,
															AllReturnsTotal,
															GridMadMedian,
															GridMadMode,
															CanopyReliefRatio,
															GridQuadraticMean,
															GridCubicMean,
															GridPA);

														if (m_DoKDEStats)
															fprintf(OutputFile, ",%i,%.4lf,%.4lf,%.4lf", KDE_ModeCount, KDE_MinMode, KDE_MaxMode, KDE_ModeRange);

														if (m_UseGlobalIdentifier) {
															fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
														}
														else {
															fprintf(OutputFile, "\n");
														}
													}
													else {
														//                   R  C  X     Y     #  min   max   ave   mode  stdv  var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L1/L2 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99
														fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
															j - CellOffsetRows,
															i - CellOffsetCols,
															x,
															y,
															GridPointCount,
															GridMin,
															GridMax,
															GridMean,
															GridMode,
															GridStdDev,
															GridVariance,
															GridCV,
															GridIQ,
															GridSkewness,
															GridKurtosis,
															GridAAD,
															GridL1,
															GridL2,
															GridL3,
															GridL4,
															((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
															((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
															((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
															Percentile[0],
															Percentile[1],
															Percentile[2],
															Percentile[4],
															Percentile[5],
															Percentile[6],
															Percentile[8],
															Percentile[10],
															Percentile[12],
															Percentile[14],
															Percentile[15],
															Percentile[16],
															Percentile[18],
															Percentile[19],
															Percentile[20]);

														if (m_UseGlobalIdentifier) {
															fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
														}
														else {
															fprintf(OutputFile, "\n");
														}
													}

													if (m_AddBufferDistance || m_AddBufferCells) {
														MinRowOutput = min(MinRowOutput, j);
														MaxRowOutput = max(MaxRowOutput, j);
														MinColOutput = min(MinColOutput, i);
														MaxColOutput = max(MaxColOutput, i);
													}
												}

												if (m_ApplyFuelModels && !CellInBuffer && m_StatParameter == ELEVATION_VALUE) {
													fprintf(FuelOutputFile, "%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
														j - CellOffsetRows,
														i - CellOffsetCols,
														-9999.0,
														-9999.0,
														-9999.0,
														-9999.0,
														x,
														y);

													if (m_UseGlobalIdentifier) {
														fprintf(FuelOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(FuelOutputFile, "\n");
													}

													if (m_AddBufferDistance || m_AddBufferCells) {
														MinRowOutput = min(MinRowOutput, j);
														MaxRowOutput = max(MaxRowOutput, j);
														MinColOutput = min(MinColOutput, i);
														MaxColOutput = max(MaxColOutput, i);
													}
												}

												if ((m_DoHeightStrata || m_DoHeightStrataIntensity) && !CellInBuffer && m_StatParameter == INTENSITY_VALUE) {
													fprintf(StrataOutputFile, "%i,%i",
														j - CellOffsetRows,
														i - CellOffsetCols);

													if (m_DoHeightStrata) {
														for (k = 0; k < m_HeightStrataCount; k++) {
															fprintf(StrataOutputFile, ",-9999,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0");
														}
													}

													if (m_DoHeightStrataIntensity) {
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															fprintf(StrataOutputFile, ",-9999,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0");
														}
													}

													if (m_UseGlobalIdentifier) {
														fprintf(StrataOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(StrataOutputFile, "\n");
													}
												}

												x += m_CellSize;
												continue;
											}

											// initialize
											//ValueSum = 0.0;
											//ValueSumSq = 0.0;
											ElevSumSquare = 0.0;
											ElevSumCube = 0.0;
											CellCount = 0;
											FirstReturnsAboveThreshold = 0;
											FirstReturnsTotal = 0;
											FirstReturnsAboveMean = 0;
											FirstReturnsAboveMode = 0;
											FirstReturnsTotalMeanMode = 0;
											AllReturnsAboveThreshold = 0;
											AllReturnsTotal = 0;
											AllReturnsAboveMean = 0;
											AllReturnsAboveMode = 0;
											AllReturnsTotalMeanMode = 0;
											MinPtIndex = -1;
											IntensityMinPtIndex = -1;
											IntensityCellCount = 0;
											KDE_ModeCount = 0;
											KDE_MaxMode = 0.0;
											KDE_MinMode = 0.0;
											KDE_ModeRange = 0.0;

											// initialize return counts
											for (k = 0; k < 10; k++) {
												ReturnCounts[k] = 0;
											}

											// ************************************************************
											// at this point, we have points within the current cell
											// compute metrics
											// ************************************************************

											// step through points within the cell and count points in cell
											// and sum values and squared values and count returns and get min/max intensity
											if (m_StatParameter == INTENSITY_VALUE) {
												GridMin = FLT_MAX;
												GridMax = -FLT_MAX;
											}

											FirstCellPoint = CurrentPointNumber;
											while (Points[CurrentPointNumber].CellNumber == CurrentCellNumber) {
												if (m_StatParameter == INTENSITY_VALUE) {
													if (IntensityMinPtIndex < 0)
														IntensityMinPtIndex = CurrentPointNumber;		// only valid when points are sorted by intensity

													IntensityCellCount++;
												}

												// count all returns
												AllReturnsTotal++;

												// do counting for cover values
												if (m_StatParameter == ELEVATION_VALUE) {
													if (Points[CurrentPointNumber].Elevation > m_HeightBreak)
														AllReturnsAboveThreshold++;

													// count 1st returns
													if (Points[CurrentPointNumber].ReturnNumber == 1) {
														FirstReturnsTotal++;
														if (Points[CurrentPointNumber].Elevation > m_HeightBreak)
															FirstReturnsAboveThreshold++;
													}
												}
												/* removed 10/20/2009
																								if (m_UseFirstReturnsForDensity && Points[CurrentPointNumber].ReturnNumber == 1 && m_StatParameter == ELEVATION_VALUE) {
																									FirstReturnsTotal ++;

																									if (Points[CurrentPointNumber].Elevation > m_HeightBreak)
																										FirstReturnsAboveThreshold ++;
																								}
																								else if (!m_UseFirstReturnsForDensity && m_StatParameter == ELEVATION_VALUE) {
																									FirstReturnsTotal ++;

																									if (Points[CurrentPointNumber].Elevation > m_HeightBreak)
																										FirstReturnsAboveThreshold ++;
																								}
												*/
												if (Points[CurrentPointNumber].Elevation > m_MinHeightForMetrics) {
													if (MinPtIndex < 0)
														MinPtIndex = CurrentPointNumber;		// only valid when points are sorted by elevation

													CellCount++;

													if (CellCount == 1) {
														oldM = newM = Points[CurrentPointNumber].Value;
														oldS = 0.0;
													}
													else {
														newM = oldM + (Points[CurrentPointNumber].Value - oldM) / CellCount;
														newS = oldS + (Points[CurrentPointNumber].Value - oldM) * (Points[CurrentPointNumber].Value - newM);

														oldM = newM;
														oldS = newS;
													}
													// do accumulation for other metrics
													//ValueSum += Points[CurrentPointNumber].Value;
													//ValueSumSq += (Points[CurrentPointNumber].Value * Points[CurrentPointNumber].Value);

													ElevSumSquare += (Points[CurrentPointNumber].Value * Points[CurrentPointNumber].Value);
													ElevSumCube += fabs((Points[CurrentPointNumber].Value * Points[CurrentPointNumber].Value * Points[CurrentPointNumber].Value));

													// count returns
													if (Points[CurrentPointNumber].ReturnNumber > 0 && Points[CurrentPointNumber].ReturnNumber < 10) {
														ReturnCounts[Points[CurrentPointNumber].ReturnNumber - 1] ++;
													}
													else {
														// if return number is <= 0 or >= 10, count as other return
														ReturnCounts[9] ++;
													}

													// do min/max for intensity
													if (m_StatParameter == INTENSITY_VALUE) {
														GridMin = min(GridMin, Points[CurrentPointNumber].Value);
														GridMax = max(GridMax, Points[CurrentPointNumber].Value);
													}
												}

												CurrentPointNumber++;

												if (CurrentPointNumber == PointCount)
													break;
											}

											// do cover calculation...need to have enough points regardless
											// of the height cutoff for metrics
											if (FirstReturnsTotal && m_StatParameter == ELEVATION_VALUE) {
												GridFirstDensity = (double)FirstReturnsAboveThreshold / (double)FirstReturnsTotal;
												GridAllFirstDensity = (double)AllReturnsAboveThreshold / (double)FirstReturnsTotal;
											}
											else {
												GridFirstDensity = -9999.0;		// changed from -1.0 1/27/2009
												GridAllFirstDensity = -9999.0;
											}

											if (AllReturnsTotal && m_StatParameter == ELEVATION_VALUE)
												GridAllDensity = (double)AllReturnsAboveThreshold / (double)AllReturnsTotal;
											else
												GridAllDensity = -9999.0;		// changed from -1.0 1/27/2009

											// 11/16/2011...need to compute strata-related stuff if we have any points in the cell
											// Prior to this revision, strata metrics were computed incorrectly for cells that had no points
											// above the minimum height...the metrics were actually the previous cell's values
											//
											// if we are doing strata, initialize
											if (m_DoHeightStrata || m_DoHeightStrataIntensity && m_StatParameter == INTENSITY_VALUE) {
												// do strata when doing intensity metrics
												// this is when we have both the return elevation and its intensity
												// when doing elevation metrics, we have only elevation

												// initialize strata counts and std dev (will be used to accumulate values)
												for (k = 0; k < MAX_NUMBER_OF_STRATA; k++) {
													ElevStrataCount[k] = 0;
													ElevStrataVariance[k] = 0.0;
													ElevStrataMean[k] = 0.0;
													ElevStrataMin[k] = DBL_MAX;
													ElevStrataMax[k] = -DBL_MAX;
													ElevStrataMedian[k] = 0.0;
													ElevStrataMode[k] = 0.0;
													ElevStrataSkewness[k] = 0.0;
													ElevStrataKurtosis[k] = 0.0;
													ElevStrataM2[k] = 0.0;
													ElevStrataM3[k] = 0.0;
													ElevStrataM4[k] = 0.0;
													for (m = 0; m < 10; m++)
														ElevStrataCountReturn[k][m] = 0;		// counts of each return in strata

													IntStrataCount[k] = 0;
													IntStrataVariance[k] = 0.0;
													IntStrataMean[k] = 0.0;
													IntStrataMin[k] = DBL_MAX;
													IntStrataMax[k] = -DBL_MAX;
													IntStrataMedian[k] = 0.0;
													IntStrataMode[k] = 0.0;
													IntStrataSkewness[k] = 0.0;
													IntStrataKurtosis[k] = 0.0;
													IntStrataM2[k] = 0.0;
													IntStrataM3[k] = 0.0;
													IntStrataM4[k] = 0.0;
													for (m = 0; m < 10; m++)
														IntStrataCountReturn[k][m] = 0;			// counts of each return in strata
												}

												// if there are points in the cell, compute the strata metrics
												if (AllReturnsTotal) {
													// resort cell data by elevation to do strata...for INTENSITY_VALUE data are 
													// sorted by the intensity stored in the value field
													qsort(&Points[FirstCellPoint], AllReturnsTotal, sizeof(PointRecord), compareCellElev);

													if (m_DoHeightStrata) {
														for (l = FirstCellPoint; l < FirstCellPoint + AllReturnsTotal; l++) {
															// figure out which strata the point is in, accumulate counts and compute mean and variance
															// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
															// modified algorithm to use (n - 1) instead of n (in final calculation outside loop) for kurtosis and skewness to match
															// the values computed in "old" logic (non-strata)
															for (k = 0; k < m_HeightStrataCount; k++) {
																if (Points[l].Elevation < m_HeightStrata[k]) {
																	// ElevStrataCount[] has total return count
																	n1 = ElevStrataCount[k];
																	ElevStrataCount[k] ++;

																	// compute mean, variance, skewness, and kurtosis
																	delta = (double)Points[l].Elevation - ElevStrataMean[k];
																	delta_n = delta / (double)ElevStrataCount[k];
																	delta_n2 = delta_n * delta_n;

																	term1 = delta * delta_n * (double)n1;
																	ElevStrataMean[k] += delta_n;
																	ElevStrataM4[k] += term1 * delta_n2 * ((double)ElevStrataCount[k] * (double)ElevStrataCount[k] - 3.0 * (double)ElevStrataCount[k] + 3.0) + 6.0 * delta_n2 * ElevStrataM2[k] - 4.0 * delta_n * ElevStrataM3[k];
																	ElevStrataM3[k] += term1 * delta_n * ((double)ElevStrataCount[k] - 2.0) - 3.0 * delta_n * ElevStrataM2[k];
																	ElevStrataM2[k] += term1;

																	// bins 0-8 have counts for returns 1-9
																	// bin 9 has count of "other" returns
																	if (Points[l].ReturnNumber < 10 && Points[l].ReturnNumber > 0) {
																		ElevStrataCountReturn[k][Points[l].ReturnNumber - 1] ++;
																	}
																	else {
																		ElevStrataCountReturn[k][9] ++;
																	}

																	// do min/max
																	ElevStrataMin[k] = min(ElevStrataMin[k], Points[l].Elevation);
																	ElevStrataMax[k] = max(ElevStrataMax[k], Points[l].Elevation);

																	break;
																}
															}
														}

														// compute the final variance
														for (k = 0; k < m_HeightStrataCount; k++) {
															if (ElevStrataCount[k]) {
																ElevStrataVariance[k] = ElevStrataM2[k] / (double)(ElevStrataCount[k] - 1);

																// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//																	ElevStrataKurtosis[k] = ((double) ElevStrataCount[k] * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]) - 3.0;
																if (ElevStrataM2[k] == 0.0) {
																	// trap the case where values for all returns in a cell are the same
																	ElevStrataKurtosis[k] = -9999.0;
																	ElevStrataSkewness[k] = -9999.0;
																}
																else {
																	ElevStrataKurtosis[k] = (((double)ElevStrataCount[k] - 1.0) * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]);
																	ElevStrataSkewness[k] = (sqrt((double)ElevStrataCount[k] - 1.0) * ElevStrataM3[k]) / sqrt(ElevStrataM2[k] * ElevStrataM2[k] * ElevStrataM2[k]);
																}
															}
														}

														// compute median and mode
														TempPointCount = FirstCellPoint;
														for (k = 0; k < m_HeightStrataCount; k++) {
															if (ElevStrataCount[k]) {
																WholePart = (int)((float)(ElevStrataCount[k] - 1) * 0.5f);
																Fraction = ((float)(ElevStrataCount[k] - 1) * 0.5f) - WholePart;

																WholePart = TempPointCount + WholePart;

																if (Fraction == 0.0) {
																	ElevStrataMedian[k] = Points[WholePart].Elevation;
																}
																else {
																	ElevStrataMedian[k] = Points[WholePart].Elevation + Fraction * (Points[WholePart + 1].Elevation - Points[WholePart].Elevation);
																}
															}
															else {
																ElevStrataMedian[k] = -9999.0;
															}

															TempPointCount += ElevStrataCount[k];
														}

														// figure out the mode using 64 bins for data
														// count values using bins
														TempPointCount = FirstCellPoint;
														for (k = 0; k < m_HeightStrataCount; k++) {
															if (ElevStrataCount[k]) {
																if (ElevStrataMax[k] != ElevStrataMin[k]) {
																	for (l = 0; l < NUMBEROFBINS; l++)
																		Bins[l] = 0;

																	for (l = TempPointCount; l < TempPointCount + ElevStrataCount[k]; l++) {
																		// trap a potential divide by zero case when there is 1 point in the strata or the min and max for the strata are the same
																		if (ElevStrataMax[k] == ElevStrataMin[k])
																			TheBin = 0;
																		else
																			TheBin = (int)((((double)Points[l].Elevation - ElevStrataMin[k]) / (ElevStrataMax[k] - ElevStrataMin[k])) * (double)(NUMBEROFBINS - 1));

																		Bins[TheBin] ++;
																	}

																	// find most frequent value
																	MaxCount = -1;
																	for (l = 0; l < NUMBEROFBINS; l++) {
																		if (Bins[l] > MaxCount) {
																			MaxCount = Bins[l];
																			TheBin = l;
																		}
																	}

																	// compute mode by un-scaling the bin number
																	ElevStrataMode[k] = ElevStrataMin[k] + ((double)TheBin / (double)(NUMBEROFBINS - 1)) * (ElevStrataMax[k] - ElevStrataMin[k]);
																}
																else {
																	ElevStrataMode[k] = ElevStrataMin[k];
																}
															}
															else {
																ElevStrataMode[k] = -9999.0;
															}

															TempPointCount += ElevStrataCount[k];
														}
													}

													if (m_DoHeightStrataIntensity) {
														for (l = FirstCellPoint; l < FirstCellPoint + AllReturnsTotal; l++) {
															// figure out which strata the point is in, accumulate counts and compute mean and variance
															// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
															for (k = 0; k < m_HeightStrataIntensityCount; k++) {
																if (Points[l].Elevation < m_HeightStrataIntensity[k]) {
																	// ElevStrataCount[] has total return count
																	n1 = IntStrataCount[k];
																	IntStrataCount[k] ++;

																	// compute mean, variance, skewness, and kurtosis
																	delta = (double)Points[l].Value - IntStrataMean[k];
																	delta_n = delta / (double)IntStrataCount[k];
																	delta_n2 = delta_n * delta_n;

																	term1 = delta * delta_n * (double)n1;
																	IntStrataMean[k] += delta_n;
																	IntStrataM4[k] += term1 * delta_n2 * ((double)IntStrataCount[k] * (double)IntStrataCount[k] - 3.0 * (double)IntStrataCount[k] + 3.0) + 6.0 * delta_n2 * IntStrataM2[k] - 4.0 * delta_n * IntStrataM3[k];
																	IntStrataM3[k] += term1 * delta_n * ((double)IntStrataCount[k] - 2.0) - 3.0 * delta_n * IntStrataM2[k];
																	IntStrataM2[k] += term1;

																	// bins 0-8 have counts for returns 1-9
																	// bin 9 has count of "other" returns
																	if (Points[l].ReturnNumber < 10 && Points[l].ReturnNumber > 0) {
																		IntStrataCountReturn[k][Points[l].ReturnNumber - 1] ++;
																	}
																	else {
																		IntStrataCountReturn[k][9] ++;
																	}

																	// do min/max
																	IntStrataMin[k] = min(IntStrataMin[k], Points[l].Value);
																	IntStrataMax[k] = max(IntStrataMax[k], Points[l].Value);

																	break;
																}
															}
														}

														// compute the final values
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															if (IntStrataCount[k]) {
																IntStrataVariance[k] = IntStrataM2[k] / (double)(IntStrataCount[k] - 1);

																// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//																	IntStrataKurtosis[k] = ((double) IntStrataCount[k] * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]) - 3.0;
																if (IntStrataM2[k] == 0.0) {
																	// trap the case where values for all returns in a cell are the same
																	IntStrataKurtosis[k] = -9999.0;
																	IntStrataSkewness[k] = -9999.0;
																}
																else {
																	IntStrataKurtosis[k] = (((double)IntStrataCount[k] - 1.0) * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]);
																	IntStrataSkewness[k] = (sqrt((double)IntStrataCount[k] - 1.0) * IntStrataM3[k]) / sqrt(IntStrataM2[k] * IntStrataM2[k] * IntStrataM2[k]);
																}
															}
														}

														// resort the list for the current cell using intensity values within each height strata
														TempPointCount = FirstCellPoint;
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															if (IntStrataCount[k]) {
																qsort(&Points[TempPointCount], (size_t)IntStrataCount[k], sizeof(PointRecord), comparePRint);
															}
															TempPointCount += IntStrataCount[k];
														}

														// compute median and mode
														TempPointCount = FirstCellPoint;
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															if (IntStrataCount[k]) {
																WholePart = (int)((float)(IntStrataCount[k] - 1) * 0.5);
																Fraction = ((float)(IntStrataCount[k] - 1) * 0.5f) - WholePart;

																WholePart = TempPointCount + WholePart;

																if (Fraction == 0.0) {
																	IntStrataMedian[k] = Points[WholePart].Value;
																}
																else {
																	IntStrataMedian[k] = Points[WholePart].Value + Fraction * (Points[WholePart + 1].Value - Points[WholePart].Value);
																}
															}
															else {
																IntStrataMedian[k] = -9999.0;
															}

															TempPointCount += IntStrataCount[k];
														}

														// figure out the mode using 64 bins for data
														// count values using bins
														TempPointCount = FirstCellPoint;
														for (k = 0; k < m_HeightStrataIntensityCount; k++) {
															if (IntStrataCount[k]) {
																if (IntStrataMax[k] != IntStrataMin[k]) {
																	for (l = 0; l < NUMBEROFBINS; l++)
																		Bins[l] = 0;

																	for (l = TempPointCount; l < TempPointCount + IntStrataCount[k]; l++) {
																		// trap a potential divide by zero case when there is 1 point in the strata or the min and max for the strata are the same
																		if (IntStrataMax[k] == IntStrataMin[k])
																			TheBin = 0;
																		else
																			TheBin = (int)((((double)Points[l].Value - IntStrataMin[k]) / (IntStrataMax[k] - IntStrataMin[k])) * (double)(NUMBEROFBINS - 1));
																		Bins[TheBin] ++;
																	}

																	// find most frequent value
																	MaxCount = -1;
																	for (l = 0; l < NUMBEROFBINS; l++) {
																		if (Bins[l] > MaxCount) {
																			MaxCount = Bins[l];
																			TheBin = l;
																		}
																	}

																	// compute mode by un-scaling the bin number
																	IntStrataMode[k] = IntStrataMin[k] + ((double)TheBin / (double)(NUMBEROFBINS - 1)) * (IntStrataMax[k] - IntStrataMin[k]);
																}
																else {
																	IntStrataMode[k] = IntStrataMin[k];
																}
															}
															else {
																IntStrataMode[k] = -9999.0;
															}

															TempPointCount += IntStrataCount[k];
														}
													}
												}
											}

											if (m_StatParameter == ELEVATION_VALUE && AllReturnsTotal >= 1 && m_ComputePA) {
												// trap some potential errors...first make sure the max point has height/elevation above 0.0
												// 5/1/2020 also added the max() code to force all point heights to be >= 0...anything with a negative height will be forced to a value of 0.0
												if (Points[FirstCellPoint + AllReturnsTotal - 1].Value > 0.0) {
													// compute percentile values for profile area
													// 5/1/2020 corrected an error...code was using Points[0].Value which is the first point out of all points for the area, not just the current cell
													SpecialElevPercentile[0] = max(0.0f, Points[FirstCellPoint].Value);

													for (k = 1; k < 100; k++) {
														WholePart = (int)((float)(AllReturnsTotal - 1) * ((float)k) / 100.0f);
														Fraction = ((float)(AllReturnsTotal - 1) * ((float)k) / 100.0f) - WholePart;

														if (Fraction == 0.0) {
															SpecialElevPercentile[k] = max(0.0f, Points[FirstCellPoint + WholePart].Value);
														}
														else {
															SpecialElevPercentile[k] = max(0.0f, Points[FirstCellPoint + WholePart].Value) + Fraction * (max(0.0f, Points[FirstCellPoint + WholePart + 1].Value) - max(0.0f, Points[FirstCellPoint + WholePart].Value));
														}
													}

													SpecialElevPercentile[100] = max(0.0f, Points[FirstCellPoint + AllReturnsTotal - 1].Value);

													// trap some potential errors... P99 must be > 0.0
													if (SpecialElevPercentile[99] > 0.0) {
														// compute profile area using composite trapezoid rule
														// I think it would be fairly easy to set this up so the user could specify the percentile value used to normalize the heights.
														// Basically, the 99 values would be replaced with a variable and the loop from 1 - 99 would run to the variable
														// The only part I'm not sure about is the area for the heights above that used for normalization. It might make sense to just 
														// use the percentiles up to the one used to normalize. the original code uses all normalized percentile heights so the 100th (max value)
														// normalized height is usually slightly greater than 1.0. Not sure if this a problem...probably not since any comparisons would be computed
														// the same way but this does allow the max height to influence the profile area so if there are high outliers, you could get a much higher PA.
														GridPA = SpecialElevPercentile[0] / SpecialElevPercentile[99];
														// this code doesn't use the 100th percentile value in the area calculation
														for (k = 1; k < 99; k++)
															GridPA += 2.0 * SpecialElevPercentile[k] / SpecialElevPercentile[99];
														GridPA += SpecialElevPercentile[99] / SpecialElevPercentile[99];

														// this code uses the 100th percentile value in the area calculation
														//for (k = 1; k < 100; k++)
														//	GridPA += 2.0 * SpecialElevPercentile[k] / SpecialElevPercentile[99];
														//GridPA += SpecialElevPercentile[100] / SpecialElevPercentile[99];

														GridPA *= 0.5;
													}
													else
														GridPA = -9999.0;
												}
												else
													GridPA = -9999.0;
											}
											else
												GridPA = -9999.0;

											// if we have enough points, compute metrics
											if (CellCount > m_MinCellPoints) {
												GridMean = newM;
												GridVariance = newS / ((double)CellCount - 1.0);
												GridStdDev = sqrt(GridVariance);
												//GridMean = ValueSum / (double)CellCount;
												//GridStdDev = sqrt((ValueSumSq - (ValueSum * ValueSum / (double)CellCount)) / ((double)CellCount - 1.0));
												//GridVariance = (ValueSumSq - (ValueSum * ValueSum / (double)CellCount)) / ((double)CellCount - 1.0);
												GridCV = GridStdDev / GridMean;

												// get min/max
												if (m_StatParameter == ELEVATION_VALUE) {
													// when doing elevation metrics, we know that the points are sorted by elevation so the 1st is
													// the minimum and the last is the maximum...MinPtIndex is the first point above the htmin
													// and CellCount is the number of points above the htmin
													GridMin = Points[MinPtIndex].Value;
													GridMax = Points[MinPtIndex + (CellCount - 1)].Value;

													GridQuadraticMean = sqrt(ElevSumSquare / (double)CellCount);
													GridCubicMean = pow(fabs(ElevSumCube) / (double)CellCount, 1.0 / 3.0);

													// compute canopy relief ratio
													if (GridMax != GridMin)
														CanopyReliefRatio = (GridMean - GridMin) / (GridMax - GridMin);
													else
														CanopyReliefRatio = 0.0;
												}

												// compute percentile related metrics...do 5, 10, 20, 25, ... classes
												// source http://www.resacorp.com/quartiles.htm...method 5
												//
												// Computing percentiles for elevation is easy given that the points are sorted by elevation
												// and we know the first point that is above the htmin
												// For intensity...not so easy. we have the points sorted by intensity but the list includes points 
												// above and below the htmin. Using the same logic as used for the elevations won't work
												//
												// 1/3/2022 there is a problem with the intensity metrics computed for all returns when the /strata option is used.
												// Percentile values are incorrect (not increasing from P01 to P99). I think this is a sorting problem that was introduced
												// when the strata option was added (long time ago...) or it may have been an issue from the start of GridMetrics.
												// The incorrect sort also affects the L-moment metrics as they also rely on sorted intensity values.
												//
												// The solution is to resort the points using the intensity values when computing the 
												// gridmetrics /strata X-100000Y-76000-IRG.dtm 2 20 test.csv X-100000Y-76000-IRG.las
												if (m_StatParameter == ELEVATION_VALUE) {
													for (p = 0; p < PercentileCount; p++) {
														k = PercentileNeeded[p];
														WholePart = (int)((float)(CellCount - 1) * ((float)k * 0.05));
														Fraction = ((float)(CellCount - 1) * ((float)k * 0.05f)) - WholePart;

														if (Fraction == 0.0) {
															Percentile[k] = Points[MinPtIndex + WholePart].Value;
														}
														else {
															Percentile[k] = Points[MinPtIndex + WholePart].Value + Fraction * (Points[MinPtIndex + WholePart + 1].Value - Points[MinPtIndex + WholePart].Value);
														}
													}

													// compute P01 and P99
													WholePart = (int)((float)(CellCount - 1) * 0.01);
													Fraction = ((float)(CellCount - 1) * 0.01f) - WholePart;

													if (Fraction == 0.0) {
														Percentile[0] = Points[MinPtIndex + WholePart].Value;
													}
													else {
														Percentile[0] = Points[MinPtIndex + WholePart].Value + Fraction * (Points[MinPtIndex + WholePart + 1].Value - Points[MinPtIndex + WholePart].Value);
													}

													WholePart = (int)((float)(CellCount - 1) * 0.99);
													Fraction = ((float)(CellCount - 1) * 0.99f) - WholePart;

													if (Fraction == 0.0) {
														Percentile[20] = Points[MinPtIndex + WholePart].Value;
													}
													else {
														Percentile[20] = Points[MinPtIndex + WholePart].Value + Fraction * (Points[MinPtIndex + WholePart + 1].Value - Points[MinPtIndex + WholePart].Value);
													}

													// assign specific percentile values to arrays
													GridIQ = Percentile[15] - Percentile[5];
												}
												else {
													// 1/3/2022 resort the points by intensity values to correct problem with percentile and L-moment metrics when the /strata option is used
													qsort(&Points[FirstCellPoint], AllReturnsTotal, sizeof(PointRecord), comparePRint);

													for (p = 0; p < PercentileCount; p++) {
														k = PercentileNeeded[p];
														WholePart = (int)((float)(CellCount - 1) * ((float)k * 0.05));
														Fraction = ((float)(CellCount - 1) * ((float)k * 0.05f)) - WholePart;

														if (Fraction == 0.0) {
															Percentile[k] = Points[IntensityMinPtIndex + WholePart].Value;
														}
														else {
															Percentile[k] = Points[IntensityMinPtIndex + WholePart].Value + Fraction * (Points[IntensityMinPtIndex + WholePart + 1].Value - Points[IntensityMinPtIndex + WholePart].Value);
														}
													}

													// compute P01 and P99
													WholePart = (int)((float)(CellCount - 1) * 0.01);
													Fraction = ((float)(CellCount - 1) * 0.01f) - WholePart;

													if (Fraction == 0.0) {
														Percentile[0] = Points[IntensityMinPtIndex + WholePart].Value;
													}
													else {
														Percentile[0] = Points[IntensityMinPtIndex + WholePart].Value + Fraction * (Points[IntensityMinPtIndex + WholePart + 1].Value - Points[IntensityMinPtIndex + WholePart].Value);
													}

													WholePart = (int)((float)(CellCount - 1) * 0.99);
													Fraction = ((float)(CellCount - 1) * 0.99f) - WholePart;

													if (Fraction == 0.0) {
														Percentile[20] = Points[IntensityMinPtIndex + WholePart].Value;
													}
													else {
														Percentile[20] = Points[IntensityMinPtIndex + WholePart].Value + Fraction * (Points[IntensityMinPtIndex + WholePart + 1].Value - Points[IntensityMinPtIndex + WholePart].Value);
													}

													// assign specific percentile values to arrays
													GridIQ = Percentile[15] - Percentile[5];
												}

												// go back through the data and accumulate info for skewness, kurtosis, and AAD
												GridSkewness = 0.0;
												GridKurtosis = 0.0;
												GridAAD = 0.0;
												if (m_StatParameter == ELEVATION_VALUE) {
													for (l = MinPtIndex; l < MinPtIndex + CellCount; l++) {
														GridSkewness += (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean);
														GridKurtosis += (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean);
														GridAAD += fabs(Points[l].Value - GridMean);
													}
												}
												else {
													for (l = IntensityMinPtIndex; l < IntensityMinPtIndex + IntensityCellCount; l++) {
														if (Points[l].Elevation > m_MinHeightForMetrics) {
															GridSkewness += (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean);
															GridKurtosis += (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean) * (Points[l].Value - GridMean);
															GridAAD += fabs(Points[l].Value - GridMean);
														}
													}
												}
												// compute skewness, kurtosis, and AAD
												// CellCount is valid for both elevation and intensity metrics
												if (GridStdDev == 0.0) {
													// trap the case where the values for all returns in the cell are the same
													GridSkewness = -9999.0;
													GridKurtosis = -9999.0;
												}
												else {
													GridSkewness = GridSkewness / (((double)CellCount - 1.0) * GridStdDev * GridStdDev * GridStdDev);
													GridKurtosis = GridKurtosis / (((double)CellCount - 1.0) * GridStdDev * GridStdDev * GridStdDev * GridStdDev);
												}
												GridAAD = GridAAD / (double)CellCount;

												// go back through the data and compute l-moments
												GridL1 = GridL2 = GridL3 = GridL4 = 0.0;
												LCount = 0;
												if (m_StatParameter == ELEVATION_VALUE) {
													for (l = MinPtIndex; l < MinPtIndex + CellCount; l++) {
														LCount++;

														CL1 = LCount - 1;
														CL2 = CL1 * (double)(LCount - 1 - 1) / 2.0;
														CL3 = CL2 * (double)(LCount - 1 - 2) / 3.0;

														CR1 = CellCount - LCount;
														CR2 = CR1 * (double)(CellCount - LCount - 1) / 2.0;
														CR3 = CR2 * (double)(CellCount - LCount - 2) / 3.0;

														GridL1 = GridL1 + Points[l].Value;
														GridL2 = GridL2 + (CL1 - CR1) * Points[l].Value;
														GridL3 = GridL3 + (CL2 - 2.0 * CL1 * CR1 + CR2) * Points[l].Value;
														GridL4 = GridL4 + (CL3 - 3.0 * CL2 * CR1 + 3.0 * CL1 * CR2 - CR3) * Points[l].Value;
													}
													C1 = CellCount;
													C2 = C1 * (double)(CellCount - 1) / 2.0;
													C3 = C2 * (double)(CellCount - 2) / 3.0;
													C4 = C3 * (double)(CellCount - 3) / 4.0;
													GridL1 = GridL1 / C1;
													GridL2 = GridL2 / C2 / 2.0;
													GridL3 = GridL3 / C3 / 3.0;
													GridL4 = GridL4 / C4 / 4.0;
												}
												else {
													for (l = IntensityMinPtIndex; l < IntensityMinPtIndex + IntensityCellCount; l++) {
														if (Points[l].Elevation > m_MinHeightForMetrics) {
															LCount++;

															CL1 = LCount - 1;
															CL2 = CL1 * (double)(LCount - 1 - 1) / 2.0;
															CL3 = CL2 * (double)(LCount - 1 - 2) / 3.0;

															CR1 = CellCount - LCount;
															CR2 = CR1 * (double)(CellCount - LCount - 1) / 2.0;
															CR3 = CR2 * (double)(CellCount - LCount - 2) / 3.0;

															GridL1 = GridL1 + Points[l].Value;
															GridL2 = GridL2 + (CL1 - CR1) * Points[l].Value;
															GridL3 = GridL3 + (CL2 - 2.0 * CL1 * CR1 + CR2) * Points[l].Value;
															GridL4 = GridL4 + (CL3 - 3.0 * CL2 * CR1 + 3.0 * CL1 * CR2 - CR3) * Points[l].Value;
														}
													}
													C1 = CellCount;
													C2 = C1 * (double)(CellCount - 1) / 2.0;
													C3 = C2 * (double)(CellCount - 2) / 3.0;
													C4 = C3 * (double)(CellCount - 3) / 4.0;
													GridL1 = GridL1 / C1;
													GridL2 = GridL2 / C2 / 2.0;
													GridL3 = GridL3 / C3 / 3.0;
													GridL4 = GridL4 / C4 / 4.0;
												}

												// check the min and max values for the cell...can only compute the mode if they are different
												if (GridMin != GridMax) {
													// divide values in to 64 bins and find the mode
													for (l = 0; l < NUMBEROFBINS; l++)
														Bins[l] = 0;

													// count values using bins
													if (m_StatParameter == ELEVATION_VALUE) {
														for (l = MinPtIndex; l < MinPtIndex + CellCount; l++) {
															// trap a potential divide by zero case when there is 1 point in the strata or the min and max for the strata are the same
															TheBin = (int)(((Points[l].Value - GridMin) / (GridMax - GridMin)) * (double)(NUMBEROFBINS - 1));
															Bins[TheBin] ++;
														}
													}
													else {
														for (l = IntensityMinPtIndex; l < IntensityMinPtIndex + IntensityCellCount; l++) {
															if (Points[l].Elevation > m_MinHeightForMetrics) {
																TheBin = (int)(((Points[l].Value - GridMin) / (GridMax - GridMin)) * (double)(NUMBEROFBINS - 1));
																Bins[TheBin] ++;
															}
														}
													}

													// find most frequent value/bin
													MaxCount = -1;						// moved declaration earlier in code 1/18/2011
													TheBin = NUMBEROFBINS - 1;			// added 1/18/2011
													for (k = 0; k < NUMBEROFBINS; k++) {
														if (Bins[k] > MaxCount) {
															MaxCount = Bins[k];
															TheBin = k;
														}
													}

													// compute mode by un-scaling the bin number
													GridMode = GridMin + ((double)TheBin / (double)(NUMBEROFBINS - 1)) * (GridMax - GridMin);
												}
												else {
													GridMode = GridMin;
												}

												// go through the data once more to compute cover above mean and mode
												FirstReturnsAboveMean = 0;
												FirstReturnsAboveMode = 0;
												FirstReturnsTotalMeanMode = 0;

												if (m_StatParameter == ELEVATION_VALUE) {
													for (l = FirstCellPoint; l < FirstCellPoint + AllReturnsTotal; l++) {
														// count all returns
														AllReturnsTotalMeanMode++;
														if (Points[l].Elevation > GridMean)
															AllReturnsAboveMean++;
														if (Points[l].Elevation > GridMode)
															AllReturnsAboveMode++;

														// count 1st returns
														if (Points[l].ReturnNumber == 1) {
															FirstReturnsTotalMeanMode++;
															if (Points[l].Elevation > GridMean)
																FirstReturnsAboveMean++;
															if (Points[l].Elevation > GridMode)
																FirstReturnsAboveMode++;
														}
													}
												}

												// compute density above mean and mode
												if (FirstReturnsTotalMeanMode && m_StatParameter == ELEVATION_VALUE) {
													GridFirstDensityMean = (double)FirstReturnsAboveMean / (double)FirstReturnsTotal;
													GridFirstDensityMode = (double)FirstReturnsAboveMode / (double)FirstReturnsTotal;
													GridAllFirstDensityMean = (double)AllReturnsAboveMean / (double)FirstReturnsTotal;
													GridAllFirstDensityMode = (double)AllReturnsAboveMode / (double)FirstReturnsTotal;
												}
												else {
													GridFirstDensityMean = -9999.0;
													GridFirstDensityMode = -9999.0;
													GridAllFirstDensityMean = -9999.0;
													GridAllFirstDensityMode = -9999.0;
												}

												if (AllReturnsTotalMeanMode && m_StatParameter == ELEVATION_VALUE) {
													GridAllDensityMean = (double)AllReturnsAboveMean / (double)AllReturnsTotal;
													GridAllDensityMode = (double)AllReturnsAboveMode / (double)AllReturnsTotal;
												}
												else {
													GridAllDensityMean = -9999.0;
													GridAllDensityMode = -9999.0;
												}

												// apply fuel models
												if (m_ApplyFuelModels) {
													// only calculate fuel params if there is overstory veg (cover > 0.0)
													if (GridFirstDensity > 0.0) {
														CanopyFuelWeight = 22.7 + (2.9 * Percentile[5]) + (-1.7 * Percentile[18]) + (106.6 * GridFirstDensity);
														CanopyFuelWeight *= CanopyFuelWeight;
														CanopyBulkDensity = -4.3 + (3.2 * GridCV) + (0.02 * Percentile[2]) + (0.13 * Percentile[5]) + (-0.12 * Percentile[18]) + (2.4 * GridFirstDensity);
														CanopyBulkDensity = exp(CanopyBulkDensity) * 1.037;		// * 1.037 is correction for log transformation
														CanopyBaseHeight = 3.2 + (19.3 * GridCV) + (0.7 * Percentile[5]) + (2.0 * Percentile[10]) + (-1.8 * Percentile[15]) + (-8.8 * GridFirstDensity);
														CanopyHeight = 2.8 + (0.25 * GridMax) + (0.25 * Percentile[5]) + (-1.0 * Percentile[10]) + (1.5 * Percentile[15]) + (3.5 * GridFirstDensity);
													}
													else {
														CanopyFuelWeight = -9999.0;
														CanopyBulkDensity = -9999.0;
														CanopyBaseHeight = -9999.0;
														CanopyHeight = -9999.0;
													}
												}

												// ********************************************** 1/24/2011
																								// compute KDE related metrics
												if (m_StatParameter == ELEVATION_VALUE) {
													int q;
													int FullPointCount = 0;
													if (m_DoKDEStats) {
														// fill ElevDiffValueList with elevation values...uses only points above the min ht
														for (q = MinPtIndex; q < MinPtIndex + CellCount; q++) {
															//														for (q = FirstCellPoint; q < FirstCellPoint + AllReturnsTotal; q ++) {
																														// compute difference from median and mode values
															MedianDiffValueList[FullPointCount] = Points[q].Elevation;

															FullPointCount++;
														}

														BW = 0.9 * (min(GridStdDev, GridIQ) / 1.34) * pow((double)FullPointCount, -0.2);
														GaussianKDE(MedianDiffValueList, FullPointCount, BW * m_KDEBandwidthMultiplier, m_KDEWindowSize, KDE_ModeCount, KDE_MinMode, KDE_MaxMode);
														KDE_ModeRange = KDE_MaxMode - KDE_MinMode;
													}

													// new code for MAD statistics...uses only points above the min ht
													FullPointCount = 0;

													for (q = MinPtIndex; q < MinPtIndex + CellCount; q++) {
														// compute difference from median and mode values
														MedianDiffValueList[FullPointCount] = (float)fabs((double)Points[q].Elevation - Percentile[10]);
														ModeDiffValueList[FullPointCount] = (float)fabs((double)Points[q].Elevation - GridMode);

														FullPointCount++;
													}

													// sort differences
													qsort(MedianDiffValueList, (size_t)FullPointCount, sizeof(float), comparefloat);
													qsort(ModeDiffValueList, (size_t)FullPointCount, sizeof(float), comparefloat);

													// compute median values
													// source http://www.resacorp.com/quartiles.htm...method 5
													WholePart = (int)((float)(FullPointCount - 1) * 0.5);
													Fraction = ((float)(FullPointCount - 1) * 0.5f) - (float)WholePart;

													if (Fraction == 0.0) {
														GridMadMedian = MedianDiffValueList[WholePart];
														GridMadMode = ModeDiffValueList[WholePart];
													}
													else {
														GridMadMedian = MedianDiffValueList[WholePart] + Fraction * (MedianDiffValueList[WholePart + 1] - MedianDiffValueList[WholePart]);
														GridMadMode = ModeDiffValueList[WholePart] + Fraction * (ModeDiffValueList[WholePart + 1] - ModeDiffValueList[WholePart]);
													}
												}
												/*												else {
																									// do strata when doing intensity metrics
																									// this is when we have both the return elevation and its intensity
																									// when doing elevation metrics, we have only elevation
																									if (m_DoHeightStrata || m_DoHeightStrataIntensity) {
																										// initialize strata counts and std dev (will be used to accumulate values)
																										for (k = 0; k < MAX_NUMBER_OF_STRATA; k ++) {
																											ElevStrataCount[k] = 0;
																											ElevStrataVariance[k] = 0.0;
																											ElevStrataMean[k] = 0.0;
																											ElevStrataMin[k] = DBL_MAX;
																											ElevStrataMax[k] = -DBL_MAX;
																											ElevStrataMedian[k] = 0.0;
																											ElevStrataMode[k] = 0.0;
																											ElevStrataSkewness[k] = 0.0;
																											ElevStrataKurtosis[k] = 0.0;
																											ElevStrataM2[k] = 0.0;
																											ElevStrataM3[k] = 0.0;
																											ElevStrataM4[k] = 0.0;
																											for (m = 0; m < 10; m ++)
																												ElevStrataCountReturn[k][m] = 0;		// counts of each return in strata

																											IntStrataCount[k] = 0;
																											IntStrataVariance[k] = 0.0;
																											IntStrataMean[k] = 0.0;
																											IntStrataMin[k] = DBL_MAX;
																											IntStrataMax[k] = -DBL_MAX;
																											IntStrataMedian[k] = 0.0;
																											IntStrataMode[k] = 0.0;
																											IntStrataSkewness[k] = 0.0;
																											IntStrataKurtosis[k] = 0.0;
																											IntStrataM2[k] = 0.0;
																											IntStrataM3[k] = 0.0;
																											IntStrataM4[k] = 0.0;
																											for (m = 0; m < 10; m ++)
																												IntStrataCountReturn[k][m] = 0;			// counts of each return in strata
																										}

																										// resort cell data by elevation to do strata...for INTENSITY_VALUE data are
																										// sorted by the intensity stored in the value field
																										qsort(&Points[FirstCellPoint], AllReturnsTotal, sizeof(PointRecord), compareCellElev);

																										if (m_DoHeightStrata) {
																											for (l = FirstCellPoint; l < FirstCellPoint + AllReturnsTotal; l ++) {
																												// figure out which strata the point is in, accumulate counts and compute mean and variance
																												// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
																												// modified algorithm to use (n - 1) instead of n (in final calculation outside loop) for kurtosis and skewness to match
																												// the values computed in "old" logic (non-strata)
																												for (k = 0; k < m_HeightStrataCount; k ++) {
																													if (Points[l].Elevation < m_HeightStrata[k]) {
																														// ElevStrataCount[] has total return count
																														n1 = ElevStrataCount[k];
																														ElevStrataCount[k] ++;

																														// compute mean, variance, skewness, and kurtosis
																														delta = (double) Points[l].Elevation - ElevStrataMean[k];
																														delta_n = delta / (double) ElevStrataCount[k];
																														delta_n2 = delta_n * delta_n;

																														term1 = delta * delta_n * (double) n1;
																														ElevStrataMean[k] += delta_n;
																														ElevStrataM4[k] += term1 * delta_n2 * ((double) ElevStrataCount[k] * (double) ElevStrataCount[k] - 3.0 * (double) ElevStrataCount[k] + 3.0) + 6.0 * delta_n2 * ElevStrataM2[k] - 4.0 * delta_n * ElevStrataM3[k];
																														ElevStrataM3[k] += term1 * delta_n * ((double) ElevStrataCount[k] - 2.0) - 3.0 * delta_n * ElevStrataM2[k];
																														ElevStrataM2[k] += term1;

																														// bins 0-8 have counts for returns 1-9
																														// bin 9 has count of "other" returns
																														if (Points[l].ReturnNumber < 10 && Points[l].ReturnNumber > 0) {
																															ElevStrataCountReturn[k][Points[l].ReturnNumber - 1] ++;
																														}
																														else {
																															ElevStrataCountReturn[k][9] ++;
																														}

																														// do min/max
																														ElevStrataMin[k] = min(ElevStrataMin[k], Points[l].Elevation);
																														ElevStrataMax[k] = max(ElevStrataMax[k], Points[l].Elevation);

																														break;
																													}
																												}
																											}

																											// compute the final variance
																											for (k = 0; k < m_HeightStrataCount; k ++) {
																												if (ElevStrataCount[k]) {
																													ElevStrataVariance[k] = ElevStrataM2[k] / (double) (ElevStrataCount[k] - 1);

																													// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
												//																	ElevStrataKurtosis[k] = ((double) ElevStrataCount[k] * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]) - 3.0;
																													ElevStrataKurtosis[k] = (((double) ElevStrataCount[k] - 1.0) * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]);
																													ElevStrataSkewness[k] = (sqrt((double) ElevStrataCount[k] - 1.0) * ElevStrataM3[k]) / sqrt(ElevStrataM2[k] * ElevStrataM2[k] * ElevStrataM2[k]);
																												}
																											}

																											// compute median and mode
																											TempPointCount = FirstCellPoint;
																											for (k = 0; k < m_HeightStrataCount; k ++) {
																												if (ElevStrataCount[k]) {
																													WholePart = (int) ((float) (ElevStrataCount[k] - 1) * 0.5);
																													Fraction = ((float) (ElevStrataCount[k] - 1) * 0.5) - WholePart;

																													WholePart = TempPointCount + WholePart;

																													if (Fraction == 0.0) {
																														ElevStrataMedian[k] = Points[WholePart].Elevation;
																													}
																													else {
																														ElevStrataMedian[k] = Points[WholePart].Elevation + Fraction * (Points[WholePart + 1].Elevation - Points[WholePart].Elevation);
																													}
																												}
																												else {
																													ElevStrataMedian[k] = -9999.0;
																												}

																												TempPointCount += ElevStrataCount[k];
																											}

																											// figure out the mode using 64 bins for data
																											// count values using bins
																											TempPointCount = FirstCellPoint;
																											for (k = 0; k < m_HeightStrataCount; k ++) {
																												if (ElevStrataCount[k]) {
																													for (l = 0; l < NUMBEROFBINS; l ++)
																														Bins[l] = 0;

																													for (l = TempPointCount; l < TempPointCount + ElevStrataCount[k]; l ++) {
																														TheBin = (int) ((((double) Points[l].Elevation - ElevStrataMin[k]) / (ElevStrataMax[k] - ElevStrataMin[k])) * (double) (NUMBEROFBINS - 1));
																														Bins[TheBin] ++;
																													}

																													// find most frequent value
																													MaxCount = -1;
																													for (l = 0; l < NUMBEROFBINS; l ++) {
																														if (Bins[l] > MaxCount) {
																															MaxCount = Bins[l];
																															TheBin = l;
																														}
																													}

																													// compute mode by un-scaling the bin number
																													ElevStrataMode[k] = ElevStrataMin[k] + ((double) TheBin / (double) (NUMBEROFBINS - 1)) * (ElevStrataMax[k] - ElevStrataMin[k]);
																												}
																												else {
																													ElevStrataMode[k] = -9999.0;
																												}

																												TempPointCount += ElevStrataCount[k];
																											}
																										}

																										if (m_DoHeightStrataIntensity) {
																											for (l = FirstCellPoint; l < FirstCellPoint + AllReturnsTotal; l ++) {
																												// figure out which strata the point is in, accumulate counts and compute mean and variance
																												// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
																												for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
																													if (Points[l].Elevation < m_HeightStrataIntensity[k]) {
																														// ElevStrataCount[] has total return count
																														n1 = IntStrataCount[k];
																														IntStrataCount[k] ++;

																														// compute mean, variance, skewness, and kurtosis
																														delta = (double) Points[l].Value - IntStrataMean[k];
																														delta_n = delta / (double) IntStrataCount[k];
																														delta_n2 = delta_n * delta_n;

																														term1 = delta * delta_n * (double) n1;
																														IntStrataMean[k] += delta_n;
																														IntStrataM4[k] += term1 * delta_n2 * ((double) IntStrataCount[k] * (double) IntStrataCount[k] - 3.0 * (double) IntStrataCount[k] + 3.0) + 6.0 * delta_n2 * IntStrataM2[k] - 4.0 * delta_n * IntStrataM3[k];
																														IntStrataM3[k] += term1 * delta_n * ((double) IntStrataCount[k] - 2.0) - 3.0 * delta_n * IntStrataM2[k];
																														IntStrataM2[k] += term1;

																														// bins 0-8 have counts for returns 1-9
																														// bin 9 has count of "other" returns
																														if (Points[l].ReturnNumber < 10 && Points[l].ReturnNumber > 0) {
																															IntStrataCountReturn[k][Points[l].ReturnNumber - 1] ++;
																														}
																														else {
																															IntStrataCountReturn[k][9] ++;
																														}

																														// do min/max
																														IntStrataMin[k] = min(IntStrataMin[k], Points[l].Value);
																														IntStrataMax[k] = max(IntStrataMax[k], Points[l].Value);

																														break;
																													}
																												}
																											}

																											// compute the final values
																											for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
																												if (IntStrataCount[k]) {
																													IntStrataVariance[k] = IntStrataM2[k] / (double) (IntStrataCount[k] - 1);

																													// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
												//																	IntStrataKurtosis[k] = ((double) IntStrataCount[k] * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]) - 3.0;
																													IntStrataKurtosis[k] = (((double) IntStrataCount[k] - 1.0) * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]);
																													IntStrataSkewness[k] = (sqrt((double) IntStrataCount[k] - 1.0) * IntStrataM3[k]) / sqrt(IntStrataM2[k] * IntStrataM2[k] * IntStrataM2[k]);
																												}
																											}

																											// resort the list for the current cell using intensity values within each height strata
																											TempPointCount = FirstCellPoint;
																											for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
																												if (IntStrataCount[k]) {
																													qsort(&Points[TempPointCount], (size_t) IntStrataCount[k], sizeof(PointRecord), comparePRint);
																												}
																												TempPointCount += IntStrataCount[k];
																											}

																											// compute median and mode
																											TempPointCount = FirstCellPoint;
																											for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
																												if (IntStrataCount[k]) {
																													WholePart = (int) ((float) (IntStrataCount[k] - 1) * 0.5);
																													Fraction = ((float) (IntStrataCount[k] - 1) * 0.5) - WholePart;

																													WholePart = TempPointCount + WholePart;

																													if (Fraction == 0.0) {
																														IntStrataMedian[k] = Points[WholePart].Value;
																													}
																													else {
																														IntStrataMedian[k] = Points[WholePart].Value + Fraction * (Points[WholePart + 1].Value - Points[WholePart].Value);
																													}
																												}
																												else {
																													IntStrataMedian[k] = -9999.0;
																												}

																												TempPointCount += IntStrataCount[k];
																											}

																											// figure out the mode using 64 bins for data
																											// count values using bins
																											TempPointCount = FirstCellPoint;
																											for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
																												if (IntStrataCount[k]) {
																													for (l = 0; l < NUMBEROFBINS; l ++)
																														Bins[l] = 0;

																													for (l = TempPointCount; l < TempPointCount + IntStrataCount[k]; l ++) {
																														TheBin = (int) ((((double) Points[l].Value - IntStrataMin[k]) / (IntStrataMax[k] - IntStrataMin[k])) * (double) (NUMBEROFBINS - 1));
																														Bins[TheBin] ++;
																													}

																													// find most frequent value
																													MaxCount = -1;
																													for (l = 0; l < NUMBEROFBINS; l ++) {
																														if (Bins[l] > MaxCount) {
																															MaxCount = Bins[l];
																															TheBin = l;
																														}
																													}

																													// compute mode by un-scaling the bin number
																													IntStrataMode[k] = IntStrataMin[k] + ((double) TheBin / (double) (NUMBEROFBINS - 1)) * (IntStrataMax[k] - IntStrataMin[k]);
																												}
																												else {
																													IntStrataMode[k] = -9999.0;
																												}

																												TempPointCount += IntStrataCount[k];
																											}
																										}
																									}
																								}
												*/
												//***********************************************
#ifdef INCLUDE_OUTLIER_IMAGE_CODE
												// do outlier analysis
												if (m_CreateOutlierImage) {
													if (GridMin < (GridMean - GridStdDev * 3.0))
														OutlierID[j][i] = 1;
												}
#endif	// INCLUDE_OUTLIER_IMAGE_CODE
											}
											else {
												GridMean = -9999.0;
												GridQuadraticMean = -9999.0;
												GridCubicMean = -9999.0;
												GridMode = -9999.0;
												GridStdDev = -9999.0;
												GridVariance = -9999.0;
												GridCV = -9999.0;
												GridSkewness = -9999.0;
												GridKurtosis = -9999.0;
												GridAAD = -9999.0;
												GridMadMedian = -9999.0;
												GridMadMode = -9999.0;
												GridIQ = -9999.0;
												GridMin = -9999.0;
												GridMax = -9999.0;
												GridFirstDensityMean = -9999.0;
												GridFirstDensityMode = -9999.0;
												GridAllDensityMean = -9999.0;
												GridAllDensityMode = -9999.0;
												GridAllFirstDensityMean = -9999.0;
												GridAllFirstDensityMode = -9999.0;
												GridL1 = GridL2 = GridL3 = GridL4 = -9999.0;
												// removed 1/27/2009											GridFirstDensity = -9999.0;
												CanopyFuelWeight = -9999.0;
												CanopyBulkDensity = -9999.0;
												CanopyBaseHeight = -9999.0;
												CanopyHeight = -9999.0;
												CanopyReliefRatio = -9999.0;

												KDE_ModeCount = -9999;
												KDE_MaxMode = -9999.0;
												KDE_MinMode = -9999.0;
												KDE_ModeRange = -9999.0;

												// set percentiles
												for (k = 0; k <= 20; k++) {
													Percentile[k] = -9999.0;
												}

												// initialize return counts
												if (CellCount <= 0) {
													for (k = 0; k < 10; k++) {
														ReturnCounts[k] = 0;
													}
												}
											}

											GridPointCount = CellCount;

											// save values to arrays
											if (m_Create_PtCountGrid)
												PtCountGrid[j][i] = GridPointCount;
											if (m_Create_PtCountTotalGrid)
												PtCountTotalGrid[j][i] = FirstReturnsTotal;
											if (m_Create_DensityCell)
												PtDensity[j][i] = (float)((double)FirstReturnsTotal / (m_CellSize * m_CellSize));
											if (m_Create_PtCountAboveGrid)
												PtCountAboveGrid[j][i] = FirstReturnsAboveThreshold;
											if (m_Create_MinGrid)
												MinGrid[j][i] = (float)GridMin;
											if (m_Create_MaxGrid)
												MaxGrid[j][i] = (float)GridMax;
											if (m_Create_MeanGrid)
												MeanGrid[j][i] = (float)GridMean;
											if (m_Create_ModeGrid)
												ModeGrid[j][i] = (float)GridMode;
											if (m_Create_StdDevGrid)
												StdDevGrid[j][i] = (float)GridStdDev;
											if (m_Create_VarianceGrid)
												VarianceGrid[j][i] = (float)GridVariance;
											if (m_Create_CVGrid)
												CVGrid[j][i] = (float)GridCV;
											if (m_Create_SkewnessGrid)
												SkewnessGrid[j][i] = (float)GridSkewness;
											if (m_Create_KurtosisGrid)
												KurtosisGrid[j][i] = (float)GridKurtosis;
											if (m_Create_AADGrid)
												AADGrid[j][i] = (float)GridAAD;
											if (m_Create_P01Grid)
												P01Grid[j][i] = Percentile[0];
											if (m_Create_P05Grid)
												P05Grid[j][i] = Percentile[1];
											if (m_Create_P10Grid)
												P10Grid[j][i] = Percentile[2];
											if (m_Create_P20Grid)
												P20Grid[j][i] = Percentile[4];
											if (m_Create_P25Grid)
												P25Grid[j][i] = Percentile[5];
											if (m_Create_P30Grid)
												P30Grid[j][i] = Percentile[6];
											if (m_Create_P40Grid)
												P40Grid[j][i] = Percentile[8];
											if (m_Create_P50Grid)
												P50Grid[j][i] = Percentile[10];
											if (m_Create_P60Grid)
												P60Grid[j][i] = Percentile[12];
											if (m_Create_P70Grid)
												P70Grid[j][i] = Percentile[14];
											if (m_Create_P75Grid)
												P75Grid[j][i] = Percentile[15];
											if (m_Create_P80Grid)
												P80Grid[j][i] = Percentile[16];
											if (m_Create_P90Grid)
												P90Grid[j][i] = Percentile[18];
											if (m_Create_P95Grid)
												P95Grid[j][i] = Percentile[19];
											if (m_Create_P99Grid)
												P99Grid[j][i] = Percentile[20];
											if (m_Create_IQGrid)
												IQGrid[j][i] = (float)GridIQ;
											if (m_Create_90m10Grid)
												p90m10Grid[j][i] = Percentile[18] - Percentile[2];
											if (m_Create_95m05Grid)
												p95m05Grid[j][i] = Percentile[18] - Percentile[2];
											if (m_Create_DensityGrid)
												DensityGrid[j][i] = (float)GridFirstDensity;
											if (m_Create_DensityMeanGrid)
												DensityGridMean[j][i] = (float)GridFirstDensityMean;
											if (m_Create_DensityModeGrid)
												DensityGridMode[j][i] = (float)GridFirstDensityMode;
											if (m_Create_R1Count)
												R1Count[j][i] = ReturnCounts[0];
											if (m_Create_R2Count)
												R2Count[j][i] = ReturnCounts[1];
											if (m_Create_R3Count)
												R3Count[j][i] = ReturnCounts[2];
											if (m_Create_R4Count)
												R4Count[j][i] = ReturnCounts[3];
											if (m_Create_R5Count)
												R5Count[j][i] = ReturnCounts[4];
											if (m_Create_R6Count)
												R6Count[j][i] = ReturnCounts[5];
											if (m_Create_R7Count)
												R7Count[j][i] = ReturnCounts[6];
											if (m_Create_R8Count)
												R8Count[j][i] = ReturnCounts[7];
											if (m_Create_R9Count)
												R9Count[j][i] = ReturnCounts[8];
											if (m_Create_ROtherCount)
												ROtherCount[j][i] = ReturnCounts[9];
											if (m_Create_AllCover)
												AllCover[j][i] = (float)GridAllDensity;
											if (m_Create_AllFirstCover)
												AllFirstCover[j][i] = (float)GridAllFirstDensity;
											if (m_Create_AllCountCover)
												AllCountCover[j][i] = AllReturnsAboveThreshold;
											if (m_Create_AllAboveMean)
												AllAboveMean[j][i] = (float)GridAllDensityMean;
											if (m_Create_AllAboveMode)
												AllAboveMode[j][i] = (float)GridAllDensityMode;
											if (m_Create_AllFirstAboveMean)
												AllFirstAboveMean[j][i] = (float)GridAllFirstDensityMean;
											if (m_Create_AllFirstAboveMode)
												AllFirstAboveMode[j][i] = (float)GridAllFirstDensityMode;
											if (m_Create_FirstAboveMeanCount)
												FirstAboveMeanCount[j][i] = FirstReturnsAboveMean;
											if (m_Create_FirstAboveModeCount)
												FirstAboveModeCount[j][i] = FirstReturnsAboveMode;
											if (m_Create_AllAboveMeanCount)
												AllAboveMeanCount[j][i] = AllReturnsAboveMean;
											if (m_Create_AllAboveModeCount)
												AllAboveModeCount[j][i] = AllReturnsAboveMode;
											if (m_Create_TotalFirstCount)
												TotalFirstCount[j][i] = FirstReturnsTotal;
											if (m_Create_TotalAllCount)
												TotalAllCount[j][i] = AllReturnsTotal;

											// have values for current cell...
											// see if cell is in "buffer"
											if (m_AddBufferDistance || m_AddBufferCells) {
												CellInBuffer = FALSE;

												//											if (m_AddBufferDistance) {
												//												if (x - xMin < m_BufferDistance || xMax - x < m_BufferDistance)
												//													CellInBuffer = TRUE;
												//												if (y - yMin < m_BufferDistance || yMax - y < m_BufferDistance)
												//													CellInBuffer = TRUE;
												//											}

												if (m_AddBufferDistance || m_AddBufferCells) {
													if (j > (rows - 1) - m_BufferCells || j < m_BufferCells)
														CellInBuffer = TRUE;

													if (i > (cols - 1) - m_BufferCells || i < m_BufferCells)
														CellInBuffer = TRUE;
												}
											}

											if (OutputFile && !CellInBuffer) {
												if (m_StatParameter == ELEVATION_VALUE) {
													//                   R  C  X     Y     #  min   max   mean  mode  stdv  var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L2/L1 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99  #1 #2 #3 #4 #5 #6 #7 #8 #9 #o %1    %a    %1af  #1 #a %     %     %     %     %     %     #  #  #  #  #  #  mmed  mmod  ccr   qave  cave
													fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%i,%i,%i,%i,%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
														j - CellOffsetRows,
														i - CellOffsetCols,
														x,
														y,
														GridPointCount,
														GridMin,
														GridMax,
														GridMean,
														GridMode,
														GridStdDev,
														GridVariance,
														GridCV,
														GridIQ,
														GridSkewness,
														GridKurtosis,
														GridAAD,
														GridL1,
														GridL2,
														GridL3,
														GridL4,
														((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
														((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
														((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
														Percentile[0],
														Percentile[1],
														Percentile[2],
														Percentile[4],
														Percentile[5],
														Percentile[6],
														Percentile[8],
														Percentile[10],
														Percentile[12],
														Percentile[14],
														Percentile[15],
														Percentile[16],
														Percentile[18],
														Percentile[19],
														Percentile[20],
														ReturnCounts[0],
														ReturnCounts[1],
														ReturnCounts[2],
														ReturnCounts[3],
														ReturnCounts[4],
														ReturnCounts[5],
														ReturnCounts[6],
														ReturnCounts[7],
														ReturnCounts[8],
														ReturnCounts[9],
														GridFirstDensity * ((GridFirstDensity >= 0.0) ? 100.0 : 1.0),
														GridAllDensity * ((GridAllDensity >= 0.0) ? 100.0 : 1.0),
														GridAllFirstDensity * ((GridAllFirstDensity >= 0.0) ? 100.0 : 1.0),
														FirstReturnsAboveThreshold,
														AllReturnsAboveThreshold,
														GridFirstDensityMean * ((GridFirstDensityMean >= 0.0) ? 100.0 : 1.0),
														GridFirstDensityMode * ((GridFirstDensityMode >= 0.0) ? 100.0 : 1.0),
														GridAllDensityMean * ((GridAllDensityMean >= 0.0) ? 100.0 : 1.0),
														GridAllDensityMode * ((GridAllDensityMode >= 0.0) ? 100.0 : 1.0),
														GridAllFirstDensityMean * ((GridAllFirstDensityMean >= 0.0) ? 100.0 : 1.0),
														GridAllFirstDensityMode * ((GridAllFirstDensityMode >= 0.0) ? 100.0 : 1.0),
														FirstReturnsAboveMean,
														FirstReturnsAboveMode,
														AllReturnsAboveMean,
														AllReturnsAboveMode,
														FirstReturnsTotal,
														AllReturnsTotal,
														GridMadMedian,
														GridMadMode,
														CanopyReliefRatio,
														GridQuadraticMean,
														GridCubicMean,
														GridPA);

													if (m_DoKDEStats)
														fprintf(OutputFile, ",%i,%.4lf,%.4lf,%.4lf", KDE_ModeCount, KDE_MinMode, KDE_MaxMode, KDE_ModeRange);

													if (m_UseGlobalIdentifier) {
														fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(OutputFile, "\n");
													}
												}
												else {
													//													fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
																										//                   R  C  X     Y     #  min   max   ave   mode  stdv  var   CV    IQ    skew  kurt  AAD   L1    L2    L3    L4    L1/L2 L3/L2 L4/L2 P01  P05  P10  P20  P25  P30  P40  P50  P60  P70  P75  P80  P90  P95  P99
													fprintf(OutputFile, "%i,%i,%.4lf,%.4lf,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
														j - CellOffsetRows,
														i - CellOffsetCols,
														x,
														y,
														GridPointCount,
														GridMin,
														GridMax,
														GridMean,
														GridMode,
														GridStdDev,
														GridVariance,
														GridCV,
														(double)(Percentile[15] - Percentile[5]),
														GridSkewness,
														GridKurtosis,
														GridAAD,
														GridL1,
														GridL2,
														GridL3,
														GridL4,
														((GridL2 != -9999.0) ? (GridL2 / GridL1) : -9999.0),
														((GridL3 != -9999.0 && GridL2 != 0.0) ? (GridL3 / GridL2) : -9999.0),
														((GridL4 != -9999.0 && GridL2 != 0.0) ? (GridL4 / GridL2) : -9999.0),
														Percentile[0],
														Percentile[1],
														Percentile[2],
														Percentile[4],
														Percentile[5],
														Percentile[6],
														Percentile[8],
														Percentile[10],
														Percentile[12],
														Percentile[14],
														Percentile[15],
														Percentile[16],
														Percentile[18],
														Percentile[19],
														Percentile[20]);

													if (m_UseGlobalIdentifier) {
														fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(OutputFile, "\n");
													}
												}

												if (m_AddBufferDistance || m_AddBufferCells) {
													MinRowOutput = min(MinRowOutput, j);
													MaxRowOutput = max(MaxRowOutput, j);
													MinColOutput = min(MinColOutput, i);
													MaxColOutput = max(MaxColOutput, i);
												}
											}

											// output fuel model results
											if (m_ApplyFuelModels && !CellInBuffer && m_StatParameter == ELEVATION_VALUE) {
												fprintf(FuelOutputFile, "%i,%i,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf",
													j - CellOffsetRows,
													i - CellOffsetCols,
													CanopyFuelWeight,
													CanopyBulkDensity,
													CanopyBaseHeight,
													CanopyHeight,
													x,
													y);

												if (m_UseGlobalIdentifier) {
													fprintf(FuelOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
												}
												else {
													fprintf(FuelOutputFile, "\n");
												}

												if (m_AddBufferDistance || m_AddBufferCells) {
													MinRowOutput = min(MinRowOutput, j);
													MaxRowOutput = max(MaxRowOutput, j);
													MinColOutput = min(MinColOutput, i);
													MaxColOutput = max(MaxColOutput, i);
												}
											}

											if ((m_DoHeightStrata || m_DoHeightStrataIntensity) && !CellInBuffer && m_StatParameter == INTENSITY_VALUE) {
												fprintf(StrataOutputFile, "%i,%i",
													j - CellOffsetRows,
													i - CellOffsetCols);

												if (m_DoHeightStrata) {
													for (k = 0; k < m_HeightStrataCount; k++) {
														if (ElevStrataCount[k] > 2)
															fprintf(StrataOutputFile, ",%i,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", ElevStrataCount[k], (double)ElevStrataCount[k] / (double)AllReturnsTotal, ElevStrataMin[k], ElevStrataMax[k], ElevStrataMean[k], ElevStrataMode[k], ElevStrataMedian[k], sqrt(ElevStrataVariance[k]), sqrt(ElevStrataVariance[k]) / ElevStrataMean[k], ElevStrataSkewness[k], ElevStrataKurtosis[k]);
														else if (ElevStrataCount[k])
															fprintf(StrataOutputFile, ",%i,%lf,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", ElevStrataCount[k], (double)ElevStrataCount[k] / (double)AllReturnsTotal, ElevStrataMean[k]);
														else
															fprintf(StrataOutputFile, ",%i,0.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", ElevStrataCount[k]);
													}
												}

												if (m_DoHeightStrataIntensity) {
													for (k = 0; k < m_HeightStrataIntensityCount; k++) {
														if (IntStrataCount[k] > 2)
															fprintf(StrataOutputFile, ",%i,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", IntStrataCount[k], (double)IntStrataCount[k] / (double)AllReturnsTotal, IntStrataMin[k], IntStrataMax[k], IntStrataMean[k], IntStrataMode[k], IntStrataMedian[k], sqrt(IntStrataVariance[k]), sqrt(IntStrataVariance[k]) / IntStrataMean[k], IntStrataSkewness[k], IntStrataKurtosis[k]);
														else if (IntStrataCount[k])
															fprintf(StrataOutputFile, ",%i,%lf,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", IntStrataCount[k], (double)IntStrataCount[k] / (double)AllReturnsTotal, IntStrataMean[k]);
														else
															fprintf(StrataOutputFile, ",%i,0.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", IntStrataCount[k]);
													}
												}

												if (m_UseGlobalIdentifier) {
													fprintf(StrataOutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
												}
												else {
													fprintf(StrataOutputFile, "\n");
												}
											}

											x += m_CellSize;

											// removed 7/24/2008 RJM
											//										if (CurrentPointNumber == PointCount)
											//											break;
										}
										y -= m_CellSize;

										// removed 7/24/2008 RJM
										//									if (CurrentPointNumber == PointCount)
										//										break;
									}

									if (OutputFile) {
										fclose(OutputFile);

										LTKCL_ReportProductFile(fs.GetFullSpec(), "Grid metrics");

										// write header file for ASCII raster...needed for georeferencing of csv file
										Ft = fs.FileTitle();
										Ft = Ft + _T("_ascii_header");
										fs.SetTitle(Ft);
										fs.SetExt(".txt");

										FILE* f = fopen(fs.GetFullSpec(), "wt");
										if (f) {
											// write header
											if (m_AddBufferDistance || m_AddBufferCells) {
												fprintf(f, "ncols %i\n", (MaxColOutput - MinColOutput) + 1);
												fprintf(f, "nrows %i\n", (MaxRowOutput - MinRowOutput) + 1);
												fprintf(f, "xllcenter %.4lf\n", xMin + (m_CellSize * (double)MinColOutput));
												fprintf(f, "yllcenter %.4lf\n", yMin + (m_CellSize * (double)MinRowOutput));
											}
											else {
												fprintf(f, "ncols %i\n", cols);
												fprintf(f, "nrows %i\n", rows);
												fprintf(f, "xllcenter %.4lf\n", xMin);
												fprintf(f, "yllcenter %.4lf\n", yMin);
											}
											fprintf(f, "cellsize %.4lf\n", m_CellSize);
											fprintf(f, "nodata_value -9999\n");

											fclose(f);

											LTKCL_ReportProductFile(fs.GetFullSpec(), "ASCII raster grid header");
										}
									}

									if (m_ApplyFuelModels && m_StatParameter == ELEVATION_VALUE) {
										fclose(FuelOutputFile);

										LTKCL_ReportProductFile(Fuelfs.GetFullSpec(), "Modeled fuel parameters");

										// write header file for ASCII raster...needed for georeferencing of csv file
										Ft = Fuelfs.FileTitle();
										Ft = Ft + _T("_ascii_header");
										Fuelfs.SetTitle(Ft);
										Fuelfs.SetExt(".txt");

										FILE* f = fopen(Fuelfs.GetFullSpec(), "wt");
										if (f) {
											// write header
											if (m_AddBufferDistance || m_AddBufferCells) {
												fprintf(f, "ncols %i\n", (MaxColOutput - MinColOutput) + 1);
												fprintf(f, "nrows %i\n", (MaxRowOutput - MinRowOutput) + 1);
												fprintf(f, "xllcenter %.4lf\n", xMin + (m_CellSize * (double)MinColOutput));
												fprintf(f, "yllcenter %.4lf\n", yMin + (m_CellSize * (double)MinRowOutput));
											}
											else {
												fprintf(f, "ncols %i\n", cols);
												fprintf(f, "nrows %i\n", rows);
												fprintf(f, "xllcenter %.4lf\n", xMin);
												fprintf(f, "yllcenter %.4lf\n", yMin);
											}
											fprintf(f, "cellsize %.4lf\n", m_CellSize);
											fprintf(f, "nodata_value -9999\n");

											fclose(f);

											LTKCL_ReportProductFile(Fuelfs.GetFullSpec(), "ASCII raster grid header for fuel parameters");
										}
									}

									// close the strata file and write header file
									if ((m_DoHeightStrata || m_DoHeightStrataIntensity) && m_StatParameter == INTENSITY_VALUE) {
										fclose(StrataOutputFile);

										LTKCL_ReportProductFile(Stratafs.GetFullSpec(), "Strata metrics");

										// write header file for ASCII raster...needed for georeferencing of csv file
										Ft = Stratafs.FileTitle();
										Ft = Ft + _T("_ascii_header");
										Stratafs.SetTitle(Ft);
										Stratafs.SetExt(".txt");

										FILE* f = fopen(Stratafs.GetFullSpec(), "wt");
										if (f) {
											// write header
											if (m_AddBufferDistance || m_AddBufferCells) {
												fprintf(f, "ncols %i\n", (MaxColOutput - MinColOutput) + 1);
												fprintf(f, "nrows %i\n", (MaxRowOutput - MinRowOutput) + 1);
												fprintf(f, "xllcenter %.4lf\n", xMin + (m_CellSize * (double)MinColOutput));
												fprintf(f, "yllcenter %.4lf\n", yMin + (m_CellSize * (double)MinRowOutput));
											}
											else {
												fprintf(f, "ncols %i\n", cols);
												fprintf(f, "nrows %i\n", rows);
												fprintf(f, "xllcenter %.4lf\n", xMin);
												fprintf(f, "yllcenter %.4lf\n", yMin);
											}
											fprintf(f, "cellsize %.4lf\n", m_CellSize);
											fprintf(f, "nodata_value -9999\n");

											fclose(f);

											LTKCL_ReportProductFile(Stratafs.GetFullSpec(), "ASCII raster grid header for strata metrics");
										}
									}

#ifdef INCLUDE_OUTLIER_IMAGE_CODE
									if (m_CreateOutlierImage) {
										CImage img;
										img.CreateImage(CSize(cols, rows), 24);

										// set pixel values
										for (i = 0; i < cols; i++) {
											for (j = 0; j < rows; j++) {
												if (OutlierID[j][i] == 1)
													img.SetImagePixel(i, j, RGB(255, 0, 0));
												else
													img.SetImagePixel(i, j, RGB(255, 255, 255));
											}
										}

										fs.SetFullSpec(m_OutputFile);
										fs.SetExt(".bmp");
										img.WriteImage(fs.GetFullSpec());

										LTKCL_ReportProductFile(fs.GetFullSpec(), "Outlier image");

										// create world file
										fs.SetExt(".bmpw");
										FILE* imgfile = fopen(fs.GetFullSpec(), "wt");
										if (imgfile) {
											fprintf(imgfile, "%lf\n", m_CellSize);
											fprintf(imgfile, "%lf\n", 0.0);
											fprintf(imgfile, "%lf\n", 0.0);
											fprintf(imgfile, "%lf\n", -m_CellSize);
											fprintf(imgfile, "%lf\n", xMin);
											fprintf(imgfile, "%lf\n", yMax);

											fclose(imgfile);
										}
									}
#endif	// INCLUDE_OUTLIER_IMAGE_CODE

									// if saving raster files...
									if (m_RasterFiles) {
										CFileSpec gfs(m_OutputFile);
										CString gFt;

										// set up file names...only the density count and cover surfaces need to have the first returns indicated
										Ft = gfs.FileTitle();
										if (m_UseFirstReturnsForAllMetrics)
											Ft = Ft + _T("_first_returns_all_metrics");
										//										else if (m_UseFirstReturnsForDensity)
										//											Ft = Ft + _T("_first_returns_density_only");
										else
											Ft = Ft + _T("_all_returns_all_metrics");

										if (m_StatParameter == ELEVATION_VALUE)
											Ft = Ft + _T("_elevation_");
										else
											Ft = Ft + _T("_intensity_");

										if (m_SaveASCIIRaster)
											gfs.SetExt(".asc");
										else
											gfs.SetExt(".dtm");

										if (m_Create_PtCountTotalGrid) {
											gFt = Ft + _T("density_count_total");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												PtCountTotalGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												PtCountTotalGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Total returns for density calculation");
										}
										if (m_Create_DensityCell) {
											gFt = Ft + _T("density_per_unit");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												PtDensity.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												PtDensity.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Total returns per unit area");
										}
										if (m_Create_PtCountAboveGrid) {
											gFt = Ft + _T("density_count_above");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												PtCountAboveGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												PtCountAboveGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Returns above height break used for density calculation");
										}
										if (m_Create_DensityGrid) {
											gFt = Ft + _T("cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												DensityGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												DensityGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Cover");
										}
										if (m_Create_DensityMeanGrid) {
											gFt = Ft + _T("cover_above_mean");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												DensityGridMean.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												DensityGridMean.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Cover");
										}
										if (m_Create_DensityModeGrid) {
											gFt = Ft + _T("cover_above_mode");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												DensityGridMode.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												DensityGridMode.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Cover");
										}
										if (m_Create_PtCountGrid) {
											gFt = Ft + _T("count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												PtCountGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												PtCountGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Point count");
										}
										if (m_Create_MinGrid) {
											gFt = Ft + _T("min");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												MinGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												MinGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Minimum value");
										}
										if (m_Create_MaxGrid) {
											gFt = Ft + _T("max");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												MaxGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												MaxGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Maximum value");
										}
										if (m_Create_MeanGrid) {
											gFt = Ft + _T("mean");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												MeanGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												MeanGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Mean value");
										}
										if (m_Create_ModeGrid) {
											gFt = Ft + _T("mode");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												ModeGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												ModeGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Mode value");
										}
										if (m_Create_StdDevGrid) {
											gFt = Ft + _T("stddev");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												StdDevGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												StdDevGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Standard deviation of values");
										}
										if (m_Create_VarianceGrid) {
											gFt = Ft + _T("variance");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												VarianceGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												VarianceGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Variance");
										}
										if (m_Create_CVGrid) {
											gFt = Ft + _T("cv");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												CVGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												CVGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Coefficient of variation");
										}
										if (m_Create_SkewnessGrid) {
											gFt = Ft + _T("skewness");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												SkewnessGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												SkewnessGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Skewness value");
										}
										if (m_Create_KurtosisGrid) {
											gFt = Ft + _T("kurtosis");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												KurtosisGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												KurtosisGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Kurtosis value");
										}
										if (m_Create_AADGrid) {
											gFt = Ft + _T("aad");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AADGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AADGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "AAD value");
										}
										if (m_Create_P01Grid) {
											gFt = Ft + _T("p01");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P01Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P01Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "1st percentile");
										}
										if (m_Create_P05Grid) {
											gFt = Ft + _T("p05");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P05Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P05Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "5th percentile");
										}
										if (m_Create_P10Grid) {
											gFt = Ft + _T("p10");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P10Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P10Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "10th percentile");
										}
										if (m_Create_P20Grid) {
											gFt = Ft + _T("p20");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P20Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P20Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "20th percentile");
										}
										if (m_Create_P25Grid) {
											gFt = Ft + _T("p25");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P25Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P25Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "25th percentile");
										}
										if (m_Create_P30Grid) {
											gFt = Ft + _T("p30");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P30Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P30Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "30th percentile");
										}
										if (m_Create_P40Grid) {
											gFt = Ft + _T("p40");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P40Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P40Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "40th percentile");
										}
										if (m_Create_P50Grid) {
											gFt = Ft + _T("p50");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P50Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P50Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "50th percentile");
										}
										if (m_Create_P60Grid) {
											gFt = Ft + _T("p60");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P60Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P60Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "60th percentile");
										}
										if (m_Create_P70Grid) {
											gFt = Ft + _T("p70");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P70Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P70Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "70th percentile");
										}
										if (m_Create_P75Grid) {
											gFt = Ft + _T("p75");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P75Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P75Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "75th percentile");
										}
										if (m_Create_P80Grid) {
											gFt = Ft + _T("p80");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P80Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P80Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "80th percentile");
										}
										if (m_Create_P90Grid) {
											gFt = Ft + _T("p90");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P90Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P90Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "90th percentile");
										}
										if (m_Create_P95Grid) {
											gFt = Ft + _T("p95");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P95Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P95Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "95th percentile");
										}
										if (m_Create_P99Grid) {
											gFt = Ft + _T("p99");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												P99Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												P99Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "99th percentile");
										}
										if (m_Create_IQGrid) {
											gFt = Ft + _T("IQ");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												IQGrid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												IQGrid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Interquartile");
										}
										if (m_Create_90m10Grid) {
											gFt = Ft + _T("p90_minus_p10");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												p90m10Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												p90m10Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "90th percentile minus 10th percentile");
										}
										if (m_Create_95m05Grid) {
											gFt = Ft + _T("p95_minus_p05");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												p95m05Grid.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												p95m05Grid.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "95th percentile minus 05th percentile");
										}
										// ****** added 11/5/2009
										if (m_Create_R1Count) {
											gFt = Ft + _T("return_1_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R1Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R1Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 1 count");
										}
										if (m_Create_R2Count) {
											gFt = Ft + _T("return_2_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R2Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R2Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 2 count");
										}
										if (m_Create_R3Count) {
											gFt = Ft + _T("return_3_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R3Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R3Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 3 count");
										}
										if (m_Create_R4Count) {
											gFt = Ft + _T("return_4_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R4Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R4Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 4 count");
										}
										if (m_Create_R5Count) {
											gFt = Ft + _T("return_5_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R5Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R5Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 5 count");
										}
										if (m_Create_R6Count) {
											gFt = Ft + _T("return_6_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R6Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R6Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 6 count");
										}
										if (m_Create_R7Count) {
											gFt = Ft + _T("return_7_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R7Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R7Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 7 count");
										}
										if (m_Create_R8Count) {
											gFt = Ft + _T("return_8_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R8Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R8Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 8 count");
										}
										if (m_Create_R9Count) {
											gFt = Ft + _T("return_9_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												R9Count.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												R9Count.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Return 9 count");
										}
										if (m_Create_ROtherCount) {
											gFt = Ft + _T("return_other_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												ROtherCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												ROtherCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Other return count");
										}
										if (m_Create_AllCover) {
											gFt = Ft + _T("all_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllCover.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllCover.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above cover ht) / (total returns)");
										}
										if (m_Create_AllFirstCover) {
											gFt = Ft + _T("all_first_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllFirstCover.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllFirstCover.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above cover ht) / (total first returns)");
										}
										if (m_Create_AllCountCover) {
											gFt = Ft + _T("all_cover_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllCountCover.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllCountCover.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Number of returns above cover ht");
										}
										if (m_Create_AllAboveMean) {
											gFt = Ft + _T("all_above_mean_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllAboveMean.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllAboveMean.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above mean ht) / (total returns)");
										}
										if (m_Create_AllAboveMode) {
											gFt = Ft + _T("all_above_mode_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllAboveMode.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllAboveMode.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above ht mode) / (total returns)");
										}
										if (m_Create_AllFirstAboveMean) {
											gFt = Ft + _T("all_first_above_mean_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllFirstAboveMean.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllFirstAboveMean.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above mean ht) / (total first returns)");
										}
										if (m_Create_AllFirstAboveMode) {
											gFt = Ft + _T("all_first_above_mode_cover");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllFirstAboveMode.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllFirstAboveMode.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "(all returns above ht mode) / (total first returns)");
										}
										if (m_Create_FirstAboveMeanCount) {
											gFt = Ft + _T("first_above_mean_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												FirstAboveMeanCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												FirstAboveMeanCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Number of first returns above mean ht");
										}
										if (m_Create_FirstAboveModeCount) {
											gFt = Ft + _T("first_above_mode_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												FirstAboveModeCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												FirstAboveModeCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Number of first returns above ht mode");
										}
										if (m_Create_AllAboveMeanCount) {
											gFt = Ft + _T("all_above_mean_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllAboveMeanCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllAboveMeanCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Number of returns above mean ht");
										}
										if (m_Create_AllAboveModeCount) {
											gFt = Ft + _T("all_above_mode_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												AllAboveModeCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												AllAboveModeCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Number of returns above ht mode");
										}
										if (m_Create_TotalFirstCount) {
											gFt = Ft + _T("total_first_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												TotalFirstCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												TotalFirstCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Total number of 1st returns");
										}
										if (m_Create_TotalAllCount) {
											gFt = Ft + _T("total_count");
											gfs.SetTitle(gFt);
											if (m_SaveASCIIRaster)
												TotalAllCount.write_to_raster_file(gfs.GetFullSpec(), 6, xMin, yMin, m_CellSize, -9999.0, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);
											else
												TotalAllCount.write_to_PlansDTM_file(gfs.GetFullSpec(), xMin, yMin, m_CellSize, MinColOutput, MinRowOutput, MaxColOutput, MaxRowOutput);

											LTKCL_ReportProductFile(gfs.GetFullSpec(), "Total number of returns");
										}
									}
									m_StatParameter++;
								}

								// free memory for MAD lists
								if (MedianDiffValueList) {
									delete[] MedianDiffValueList;
								}
								if (ModeDiffValueList) {
									delete[] ModeDiffValueList;
								}
								// *************************************************
							}
							else {
								LTKCL_PrintStatus("No returns within grid extent");
								m_nRetCode = 7;
							}

							// delete points array
							delete[] Points;
							Points = NULL;

							// point cloud metrics are complete...still have ground models loaded that overlap the analysis extent
							// look at /topo distance to see if we need to load more models (same logic but different extent)
							// need to check if model overlaps extent but is not loaded...there may be more memory since points are gone
							// if using models from disk (switch on), still need to check using expanded extent
							// once we have the models available, compute topo metrics and output to separate file
							if (!m_nRetCode && m_DoTopoMetrics) {
								// check the buffer size and topo cell size and pick the one that gives the largest area
								double MinTopoX = xMin;
								double MinTopoY = yMin;
								double MaxTopoX = xMax;
								double MaxTopoY = yMax;

								if (m_BufferDistance < m_TopoDistance) {
									// adjust min/max to cover cells outside the buffered area extent
									MinTopoX = MinTopoX - (m_TopoDistance - m_BufferDistance);
									MinTopoY = MinTopoY - (m_TopoDistance - m_BufferDistance);
									MaxTopoX = MaxTopoX + (m_TopoDistance - m_BufferDistance);
									MaxTopoY = MaxTopoY + (m_TopoDistance - m_BufferDistance);

									if (m_Terrain.IsValid()) {
										m_Terrain.UnloadData();
										m_Terrain.PreLoadData(MinTopoX, MinTopoY, MaxTopoX, MaxTopoY, m_CreateInternalDTM);

										// print verbose info
										int ModelsInMemory = 0;
										for (i = 0; i < m_Terrain.GetModelCount(); i++) {
											if (m_Terrain.AreModelElevationsInMemory(i))
												ModelsInMemory++;
										}

										if (ModelsInMemory) {
											m_csStatus.Format("Loaded %i ground model(s) into memory for TOPO metrics:", ModelsInMemory);
											LTKCL_PrintVerboseStatus(m_csStatus);
											for (i = 0; i < m_Terrain.GetModelCount(); i++) {
												if (m_Terrain.AreModelElevationsInMemory(i)) {
													m_csStatus.Format("   %s (%i of %i)", (LPCSTR) m_Terrain.GetModelFileName(i), i + 1, m_GroundFileCount);
													LTKCL_PrintVerboseStatus(m_csStatus);
												}
											}
										}
										else if (m_Terrain.InternalModelIsValid()) {
											int ModelsUsedForInternal = 0;
											for (i = 0; i < m_Terrain.GetModelCount(); i++) {
												if (m_Terrain.WasModelUsedForInternalModel(i))
													ModelsUsedForInternal++;
											}

											if (ModelsUsedForInternal) {
												m_csStatus.Format("Used %i ground model(s) to build internal model for TOPO metrics:", ModelsUsedForInternal);
												LTKCL_PrintVerboseStatus(m_csStatus);
												for (i = 0; i < m_Terrain.GetModelCount(); i++) {
													if (m_Terrain.WasModelUsedForInternalModel(i)) {
														m_csStatus.Format("   %s (%i of %i)", (LPCSTR) m_Terrain.GetModelFileName(i), i + 1, m_GroundFileCount);
														LTKCL_PrintVerboseStatus(m_csStatus);
													}
												}
											}
											LTKCL_PrintVerboseStatus("");
										}
									}
								}

								// go through the ground models and reload those that overlap the extent
/*								if (m_UseGround && m_GroundDTM != NULL) {
									if (!m_GroundFromDisk) {
										for (i = 0; i < m_GroundFileCount; i ++) {
											if (RectanglesIntersect(m_GroundDTM[i].OriginX(), m_GroundDTM[i].OriginY(), m_GroundDTM[i].OriginX() + m_GroundDTM[i].Width(), m_GroundDTM[i].OriginY() + m_GroundDTM[i].Height(), MinTopoX, MinTopoY, MaxTopoX, MaxTopoY)) {
												if (!m_GroundDTM[i].ElevationsAreLoaded()) {
													m_GroundDTM[i].LoadElevations(m_GroundFiles[i], TRUE, FALSE);	// read elevations into memory
												}

												// if full read failed, set up for patch access
												if (!m_GroundDTM[i].IsValid()) {
													m_GroundDTM[i].LoadElevations(m_GroundFiles[i]);		// patch access

													m_csStatus.Format("   Surface model (%i of %i) accessed from disk: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
												}
												else {
													m_csStatus.Format("   Loaded surface model (%i of %i) into memory: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
												}
												LTKCL_PrintVerboseStatus(m_csStatus);
											}
										}
									}
									else {
										for (i = 0; i < m_GroundFileCount; i ++) {
											if (RectanglesIntersect(m_GroundDTM[i].OriginX(), m_GroundDTM[i].OriginY(), m_GroundDTM[i].OriginX() + m_GroundDTM[i].Width(), m_GroundDTM[i].OriginY() + m_GroundDTM[i].Height(), MinTopoX, MinTopoY, MaxTopoX, MaxTopoY)) {
												m_csStatus.Format("   Surface model (%i of %i) accessed from disk: %s", i + 1, m_GroundFileCount, m_GroundFiles[i]);
												LTKCL_PrintVerboseStatus(m_csStatus);
											}
										}
									}
								}
*/
								double x, y;
								double surfacevalue;
								double Z[10];
								double ATerm, BTerm, CTerm, DTerm, ETerm, FTerm, GTerm, HTerm, ITerm;
								double SlopePercent, SlopeDegrees, SlopeRadians, AspectDegrees, AspectRadians, ProfC, PlanC, Curvature, SRI;
								double AdjustedAspectRadians;
								double LatitudeRadians = m_TopoLatitude / (180.0 / 3.1415926535);
								int terms;

								// set filename for topo_metrics
								CFileSpec fs(m_OutputFile);
								CString Ft;
								Ft = fs.FileTitle();
								Ft = Ft + _T("_topo_metrics");
								fs.SetTitle(Ft);
								fs.SetExt(".csv");

								// open output file and write ouput header
								FILE* OutputFile;
								OutputFile = NULL;
								if (!m_NoCSVFile)
									OutputFile = fopen(fs.GetFullSpec(), "wt");

								if (OutputFile) {
									// printf file header
									fprintf(OutputFile, "row,col,X,Y,Elevation,Slope (degrees),Aspect (degrees azimuth),Profile curvature * 100,Plan curvature * 100,Solar Radiation Index,Overall Curvature");
									if (m_UseGlobalIdentifier) {
										fprintf(OutputFile, ",Identifier\n");
									}
									else {
										fprintf(OutputFile, "\n");
									}

									// calculate the analysis area grid and compute topo attributes for the center point
									// step through cells and compute metric for each cell with LIDAR points
									//
									// topo attributes are computed for the same grid as the metrics. However, the cell values are computed
									// using the cell size given by m_TopoDistance
									y = (yMin)+(double)(rows - 1) * m_CellSize;
									//									y = (yMin + m_CellSize / 2.0) + (double) (rows - 1) * m_CellSize;
									for (j = rows - 1; j >= 0; j--) {
										x = xMin;
										//										x = xMin + m_CellSize / 2.0;
										for (i = 0; i < cols; i++) {
											// interpolate elevation for cell values
											// get surface value
											surfacevalue = m_Terrain.InterpolateElev(x, y);
											//											surfacevalue = InterpolateGroundElevation(x, y);

																						// compute topo metrics
																						// if sample point doesn't have a valid surface value, don't create an output file
																						// ...don't want to delete a "good" file created during a previous run
											if (surfacevalue < 0.0) {
												// see if cell is in "buffer"
												CellInBuffer = FALSE;
												if (m_AddBufferDistance || m_AddBufferCells) {
													if (j > (rows - 1) - m_BufferCells || j < m_BufferCells)
														CellInBuffer = TRUE;

													if (i > (cols - 1) - m_BufferCells || i < m_BufferCells)
														CellInBuffer = TRUE;
												}

												if (!CellInBuffer) {
													fprintf(OutputFile, "%i,%i,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf", j - CellOffsetRows, i - CellOffsetCols, x, y, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0);
													if (m_UseGlobalIdentifier) {
														fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
													}
													else {
														fprintf(OutputFile, "\n");
													}
												}

												x += m_CellSize;
												continue;
											}

											// get elevations of 8 grid points...use sample location as center point
											Z[5] = surfacevalue;

											Z[1] = m_Terrain.InterpolateElev(x - m_TopoDistance, y + m_TopoDistance);
											Z[2] = m_Terrain.InterpolateElev(x, y + m_TopoDistance);
											Z[3] = m_Terrain.InterpolateElev(x + m_TopoDistance, y + m_TopoDistance);
											Z[4] = m_Terrain.InterpolateElev(x - m_TopoDistance, y);
											Z[6] = m_Terrain.InterpolateElev(x + m_TopoDistance, y);
											Z[7] = m_Terrain.InterpolateElev(x - m_TopoDistance, y - m_TopoDistance);
											Z[8] = m_Terrain.InterpolateElev(x, y - m_TopoDistance);
											Z[9] = m_Terrain.InterpolateElev(x + m_TopoDistance, y - m_TopoDistance);
											//											Z[1] = InterpolateGroundElevation(x - m_TopoDistance, y + m_TopoDistance);
											//											Z[2] = InterpolateGroundElevation(x, y + m_TopoDistance);
											//											Z[3] = InterpolateGroundElevation(x + m_TopoDistance, y + m_TopoDistance);
											//											Z[4] = InterpolateGroundElevation(x - m_TopoDistance, y);
											//											Z[6] = InterpolateGroundElevation(x + m_TopoDistance, y);
											//											Z[7] = InterpolateGroundElevation(x - m_TopoDistance, y - m_TopoDistance);
											//											Z[8] = InterpolateGroundElevation(x, y - m_TopoDistance);
											//											Z[9] = InterpolateGroundElevation(x + m_TopoDistance, y - m_TopoDistance);

																						// check for points outside DTM...set their elevation to the center point elevation
											for (terms = 1; terms < 10; terms++) {
												if (Z[terms] < 0.0)
													Z[terms] = surfacevalue;
											}

											// compute the terms
											ATerm = ((Z[1] + Z[3] + Z[7] + Z[9]) / 4.0 - (Z[2] + Z[4] + Z[6] + Z[8]) / 2.0 + Z[5]) / (m_TopoDistance * m_TopoDistance * m_TopoDistance * m_TopoDistance);
											BTerm = ((Z[1] + Z[3] - Z[7] - Z[9]) / 4.0 - (Z[2] - Z[8]) / 2.0) / (m_TopoDistance * m_TopoDistance * m_TopoDistance);
											CTerm = ((-Z[1] + Z[3] - Z[7] + Z[9]) / 4.0 + (Z[4] - Z[6]) / 2.0) / (m_TopoDistance * m_TopoDistance * m_TopoDistance);
											DTerm = ((Z[4] + Z[6]) / 2.0 - Z[5]) / (m_TopoDistance * m_TopoDistance);
											ETerm = ((Z[2] + Z[8]) / 2.0 - Z[5]) / (m_TopoDistance * m_TopoDistance);
											FTerm = (-Z[1] + Z[3] + Z[7] - Z[9]) / (4.0 * m_TopoDistance * m_TopoDistance);
											GTerm = (-Z[4] + Z[6]) / (2.0 * m_TopoDistance);
											HTerm = (Z[2] - Z[8]) / (2.0 * m_TopoDistance);
											ITerm = Z[5];

											// compute the indices
											SlopePercent = sqrt(GTerm * GTerm + HTerm * HTerm) * 100.0;
											SlopeRadians = atan(SlopePercent / 100.0);
											SlopeDegrees = SlopeRadians * (180.0 / 3.1415926535);
											AspectRadians = atan2(-HTerm, -GTerm);
											AspectDegrees = fmod(360.0 - (AspectRadians * (180.0 / 3.1415926535)) + 90.0, 360.0);
											ProfC = 100.0 * -2.0 * ((DTerm * GTerm * GTerm + ETerm * HTerm * HTerm - FTerm * GTerm * HTerm) / (GTerm * GTerm + HTerm * HTerm));
											PlanC = 100.0 *  2.0 * ((DTerm * HTerm * HTerm + ETerm * GTerm * GTerm - FTerm * GTerm * HTerm) / (GTerm * GTerm + HTerm * HTerm));
											Curvature = 100.0 * -2.0 * (DTerm + ETerm);

											AdjustedAspectRadians = (180.0 - AspectDegrees) / (180.0 / 3.1415926535);
											//	AzimuthDegrees = (360.0 - atan2(Y, X) * (180.0 / 3.1415926535) - 90.0);

											SRI = cos(LatitudeRadians) * cos(SlopeRadians) + sin(LatitudeRadians) * sin(SlopeRadians) * cos(AdjustedAspectRadians);
											// adjust so range is from 0 to 2.0
											SRI += 1.0;

											// see if cell is in "buffer"
											CellInBuffer = FALSE;
											if (m_AddBufferDistance || m_AddBufferCells) {
												if (j > (rows - 1) - m_BufferCells || j < m_BufferCells)
													CellInBuffer = TRUE;

												if (i > (cols - 1) - m_BufferCells || i < m_BufferCells)
													CellInBuffer = TRUE;
											}

											if (!CellInBuffer) {
												// output topo metrics
												fprintf(OutputFile, "%i,%i,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf,%.6lf", j - CellOffsetRows, i - CellOffsetCols, x, y, surfacevalue, SlopeDegrees, AspectDegrees, ProfC, PlanC, SRI, Curvature);
												if (m_UseGlobalIdentifier) {
													fprintf(OutputFile, ",%s\n", (LPCTSTR)m_GlobalIdentifier);
												}
												else {
													fprintf(OutputFile, "\n");
												}
											}

											x += m_CellSize;
										}
										y -= m_CellSize;
									}
									fclose(OutputFile);

									// report product
									LTKCL_ReportProductFile(fs.GetFullSpec(), "Topographic attributes");

									// write header file for ASCII raster...needed for georeferencing of csv file
									Ft = fs.FileTitle();
									Ft = Ft + _T("_ascii_header");
									fs.SetTitle(Ft);
									fs.SetExt(".txt");

									FILE* f = fopen(fs.GetFullSpec(), "wt");
									if (f) {
										// write header
										if (m_AddBufferDistance || m_AddBufferCells) {
											fprintf(f, "ncols %i\n", (MaxColOutput - MinColOutput) + 1);
											fprintf(f, "nrows %i\n", (MaxRowOutput - MinRowOutput) + 1);
											fprintf(f, "xllcenter %.4lf\n", xMin + (m_CellSize * (double)MinColOutput));
											fprintf(f, "yllcenter %.4lf\n", yMin + (m_CellSize * (double)MinRowOutput));
										}
										else {
											fprintf(f, "ncols %i\n", cols);
											fprintf(f, "nrows %i\n", rows);
											fprintf(f, "xllcenter %.4lf\n", xMin);
											fprintf(f, "yllcenter %.4lf\n", yMin);
										}
										fprintf(f, "cellsize %.4lf\n", m_CellSize);
										fprintf(f, "nodata_value -9999\n");

										fclose(f);

										LTKCL_ReportProductFile(fs.GetFullSpec(), "ASCII raster grid header");
									}
								}
								else {
									if (!m_NoCSVFile) {
										CString csTemp;
										csTemp.Format("Couldn't open file for topo metrics: %s", (LPCSTR) fs.GetFullSpec());
										LTKCL_PrintStatus(csTemp);
										m_nRetCode = 7;
									}
								}
							}
						}
						else {
							// can't allocate memory for points
							LTKCL_PrintStatus("Too many points (couldn't allocate memory for point list)...no metrics computed");
							m_nRetCode = 5;
						}
					}
					else {
						LTKCL_PrintStatus("LIDAR data file contains no points...no metrics computed");
						m_nRetCode = 6;
					}
				}
			}
			LTKCL_PrintEndReport(m_nRetCode);
		}

		// unload LAZ/LAS dll
		laszip_unload_dll();
	}

	// free up memory for ground models
	if (m_UseGround && m_Terrain.IsValid()) {
		m_Terrain.Destroy();
	}
	//	if (m_UseGround && m_GroundDTM != NULL) {
	//		// clean up memory asociated with ground models
	//		for (int i = 0; i < m_GroundFileCount; i ++)
	//			m_GroundDTM[i].Destroy();
	//
	//		// deallocate memory for models
	//		delete [] m_GroundDTM;
	//	}

	return(m_nRetCode);
}

#include "command_line_core_functions.cpp"

double InterpolateGroundElevation(double X, double Y)
{
	double groundelev = -1.0;

	if (m_UseGround) {
		groundelev = m_Terrain.InterpolateElev(X, Y);
		//		// get elevation from first model covering the XY point
		//		for (int i = 0; i < m_GroundFileCount; i ++) {
		//			groundelev = m_GroundDTM[i].InterpolateElev(X, Y);
		//			if (groundelev >= 0.0)
		//				break;
		//		}
	}
	return(groundelev);
}

#define STEPS	512
#define MIN_KDE_POINTS	10
#define MIN_KDE_RANGE	3.0

void GaussianKDE(float* PointData, int Pts, double BW, double SmoothWindow, int& ModeCount, double& MinMode, double& MaxMode)
{
	int i, j;
	double MinElev, MaxElev;
	double DataMinElev, DataMaxElev;
	int UseSmoothedCurve = TRUE;
	int ModeCnt;

	if (SmoothWindow == 0.0)
		UseSmoothedCurve = FALSE;

	// get the min/max elevation
	MinElev = DBL_MAX;
	MaxElev = -DBL_MAX;
	for (i = 0; i < Pts; i++) {
		MinElev = min(MinElev, (double)PointData[i]);
		MaxElev = max(MaxElev, (double)PointData[i]);
	}

	// save actual data range
	DataMinElev = MinElev;
	DataMaxElev = MaxElev;

	// check for meaningful data
	if (Pts < MIN_KDE_POINTS || (MaxElev - MinElev) < SmoothWindow || (MaxElev - MinElev) < MIN_KDE_RANGE) {
		ModeCount = 0;
		MinMode = 0.0;
		MaxMode = 0.0;

		return;
	}

	// adjust min/max
	MinElev -= BW * 3.0;
	MaxElev += BW * 3.0;

	// move memory to heap...
	double* X = new double[STEPS];		// elevation/height
	double* Y = new double[STEPS];		// density
	double* SmoothX = new double[STEPS];		// elevation/height
	double* SmoothY = new double[STEPS];		// density
	char* sign = new char[STEPS];	// + or -
	//double X[STEPS];		// elevation/height
	//double Y[STEPS];		// density
	//double SmoothX[STEPS];		// elevation/height
	//double SmoothY[STEPS];		// density
	//char sign[STEPS];	// + or -
	double Step = (MaxElev - MinElev) / (double)(STEPS - 1);
	double Constant1 = 1.0 / ((double)Pts * sqrt(2.0 * 3.141592653589793 * BW * BW));
	double Constant2 = 2.0 * BW * BW;
	double ExpTerm;
	double AveY;		// used for sliding window

	// compute probabilites
	double Ht = MinElev;
	for (i = 0; i < STEPS; i++) {
		ExpTerm = 0.0;
		for (j = 0; j < Pts; j++) {
			ExpTerm += exp(-1.0 * (Ht - (double)PointData[j]) * (Ht - (double)PointData[j]) / Constant2);
		}

		X[i] = Ht;
		Y[i] = Constant1 * ExpTerm;

		Ht += Step;

		//		printf("%.4lf,%.4lf\n", X[i], Y[i]);
	}

	// do a sliding window average to smooth
	if (UseSmoothedCurve) {
		int EndHalfWindow;
		int CellCnt;
		int HalfWindow = (int)(SmoothWindow / Step);

		// force HalfWindow to be odd to make sure window is centered on the point being modified
		if (HalfWindow % 2 == 0)
			HalfWindow++;

		EndHalfWindow = 0;
		for (i = 0; i < HalfWindow; i++) {
			AveY = 0.0;
			CellCnt = 0;
			for (j = i - EndHalfWindow; j <= i + EndHalfWindow; j++) {
				//		for (j = i - HalfWindow; j <= i + HalfWindow; j ++) {
				if (j >= 0) {
					AveY += Y[j];
					CellCnt++;
				}
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double)CellCnt);

			EndHalfWindow++;
		}

		// this loop should handle only complete windows...window fits within the array bounds
		for (i = HalfWindow; i < STEPS - HalfWindow; i++) {
			if (i == HalfWindow) {
				// first time...need to get all values
				AveY = 0.0;
				CellCnt = 0;
				for (j = i - HalfWindow; j <= i + HalfWindow; j++) {
					if (j >= 0 && j < STEPS) {
						AveY += Y[j];
						CellCnt++;
					}
				}
			}
			else {
				// after first time only need to change first and last values
				AveY -= Y[(i - HalfWindow) - 1];		// subtract 1 since i has been advanced by 1 since "group" was formed
				AveY += Y[(i + HalfWindow)];
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double)CellCnt);
		}

		EndHalfWindow = 0;
		for (i = STEPS - 1; i >= max(HalfWindow, STEPS - HalfWindow); i--) {
			//	for (i = STEPS - HalfWindow; i < STEPS; i ++) {
			AveY = 0.0;
			CellCnt = 0;
			for (j = i - EndHalfWindow; j <= i + EndHalfWindow; j++) {
				//		for (j = i - HalfWindow; j <= i + HalfWindow; j ++) {
				if (j >= 0 && j < STEPS) {
					AveY += Y[j];
					CellCnt++;
				}
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double)CellCnt);

			EndHalfWindow++;
		}
	}

	// figure out the signs
	int FirstMin = FALSE;
	sign[0] = -1;
	sign[STEPS - 1] = 1;
	if (!UseSmoothedCurve) {
		for (i = 0; i < STEPS; i++) {
			// make sure we don't do a mode outside the actual data range
			if (X[i] >= DataMinElev && X[i] <= DataMaxElev) {
				if (!FirstMin) {
					sign[i] = -1;
					FirstMin = TRUE;
				}
				else if (Y[i] > Y[i - 1])
					sign[i] = 1;
				else if (Y[i] < Y[i - 1])
					sign[i] = -1;
				else
					sign[i] = 0;
			}
			else
				sign[i] = 0;
		}
	}
	else {
		// do signs using smoothed values
		for (i = 0; i < STEPS; i++) {
			// make sure we don't do a mode outside the actual data range
			if (SmoothX[i] >= DataMinElev && SmoothX[i] <= DataMaxElev) {
				if (!FirstMin) {
					sign[i] = -1;
					FirstMin = TRUE;
				}
				else if (SmoothY[i] > SmoothY[i - 1])
					sign[i] = 1;
				else if (SmoothY[i] < SmoothY[i - 1])
					sign[i] = -1;
				else
					sign[i] = 0;
			}
			else
				sign[i] = 0;
		}
	}

	// get mode values
	double Modes[64];
	double ModeProb[64];
	int ModeType[64];
	ModeCnt = 0;
	for (i = 1; i < STEPS; i++) {
		// detect transitions from positive slope to flat or negative slope...peaks
		if (sign[i] != sign[i - 1] && sign[i - 1] == 1) {
			Modes[ModeCnt] = X[i - 1];
			ModeProb[ModeCnt] = Y[i - 1];
			ModeType[ModeCnt] = 1;

			//			printf("%8lf %.8lf %.8lf 255 0 0\n", 0.0, Y[i] * 100.0, X[i]);
			ModeCnt++;
		}

		// detect transitions from negative slope to flat or positive slope...valleys
		if (sign[i] != sign[i - 1] && sign[i - 1] == -1) {
			Modes[ModeCnt] = X[i - 1];
			ModeProb[ModeCnt] = Y[i - 1];
			ModeType[ModeCnt] = -1;

			ModeCnt++;
		}

		// check for too many modes
		if (ModeCnt >= 63)
			break;
	}

	if (ModeCnt >= 63) {
		ModeCount = 0;
		MinMode = 0.0;
		MaxMode = 0.0;

		delete[] X;
		delete[] Y;
		delete[] SmoothX;
		delete[] SmoothY;
		delete[] sign;

		return;
	}

	// get the min/max mode heights for peaks
	ModeCount = 0;
	MinMode = DBL_MAX;
	MaxMode = -DBL_MAX;
	for (i = 0; i < ModeCnt; i++) {
		if (ModeType[i] == 1 && (Modes[i] >= DataMinElev && Modes[i] <= DataMaxElev)) {
			MinMode = min(MinMode, Modes[i]);
			MaxMode = max(MaxMode, Modes[i]);

			ModeCount++;
		}
	}
	/*
		// output summary
		FILE* f = fopen("summary.csv", "wt");
		if (f) {
			fprintf(f, "Data minimum: %.4f\n", DataMinElev);
			fprintf(f, "Data maximum: %.4f\n", DataMaxElev);
			fprintf(f, "%i modes\n", ModeCnt);
			fprintf(f, "Minimum mode: %.4lf\n", MinMode);
			fprintf(f, "Maximum mode: %.4lf\n", MaxMode);
			fprintf(f, "Modes:\n");
			for (i = 0; i < ModeCnt; i ++) {
				fprintf(f, "   %2i   %3.4lf   %3.4lf   %2i\n", i + 1, Modes[i], ModeProb[i], ModeType[i]);
			}
			for (i = 0; i < STEPS; i ++) {
				fprintf(f, "%.8lf %.8lf %i\n", Y[i] * 100, X[i], sign[i]);
			}
			fclose(f);
		}

		// "raw" KDE values
		for (i = 0; i < STEPS; i ++) {
			if (Color == 0)
				printf("%8lf %.8lf %.8lf 255 255 255\n", 0.0, Y[i] * 1000, X[i]);
			else if (Color == 1)
				printf("%8lf %.8lf %.8lf 255 0 0\n", 0.0, Y[i] * 100, X[i]);
			else if (Color == 2)
				printf("%8lf %.8lf %.8lf 0 255 0\n", 0.0, Y[i] * 100, X[i]);
			else if (Color == 3)
				printf("%8lf %.8lf %.8lf 255 255 0\n", 0.0, Y[i] * 100, X[i]);
			else if (Color == 4)
				printf("%8lf %.8lf %.8lf 0 255 255\n", 0.0, Y[i] * 100, X[i]);
		}

		// smoothed values
		if (UseSmoothedCurve) {
			for (i = 0; i < STEPS; i ++) {
				if (Color == 0)
					printf("%8lf %.8lf %.8lf 255 0 0\n", 0.0, SmoothY[i] * 1000, SmoothX[i]);
			}
		}

		// mode lines
		double Yval;
		double YStep = 0.001;
	//	double YStep = (MaxModeProb - MinModeProb) / 11.0;
		if (YStep == 0.0)
			YStep = 0.001;
		for (i = 0; i < ModeCnt; i ++) {
			if (ModeType[i] == 1) {
				Yval = ModeProb[i] - (YStep * 5.5);
				for (j = -5; j <= 5; j ++) {
					printf("%8lf %.8lf %.8lf 255 255 0\n", 0.0, Yval * 1000.0, Modes[i]);
					Yval += YStep;
				}
			}
			if (ModeType[i] == -1) {
				Yval = ModeProb[i] - (YStep * 5.5);
				for (j = -5; j <= 5; j ++) {
					printf("%8lf %.8lf %.8lf 0 255 0\n", 0.0, Yval * 1000.0, Modes[i]);
					Yval += YStep;
				}
			}
		}
	*/
	delete[] X;
	delete[] Y;
	delete[] SmoothX;
	delete[] SmoothY;
	delete[] sign;
}
