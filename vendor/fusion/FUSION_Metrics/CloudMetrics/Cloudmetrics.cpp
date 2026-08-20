// cloudmetrics.cpp : Defines the entry point for the console application.
//
// Version info
//
// V1.3 Pre-January 2007 clean-up
//
// V1.4 Added logic to check for existing output file and automatically set the NewOutputFile flag
//      if the file does not exist
//
//      Added output of 5th and 95 percentile values for elevation and intensity
//
//      Fixed problem with /id switch...parsing logic failed if there were no numbers in the file title
//      now the ID is set to 0 if there are no numbers
//
// V1.5 2/8/2007
//      Changed reporting logic so the min. max, and mean values are reported when there are points in the
//      data file...previous version required 4 or more points
//
//      Added /highpoint option to produce output with only #pts and highest point XYZ...used for individual
//      tree samples
//
//      Removed trailing comma from CSV output
//
// V1.6 2/8/2007
//      Added htmin:# switch to restrict statistics to points at or above the specified height...
//      used only when data sample has been normalized using a ground surface model
//
// V1.7 11/4/2008
//		Added /firstreturn switch to use only first returns to compute metrics. Previous versions used
//		the /firstinpulse switch to identify the first return in a pulse which was not necessarily the first
//		return due to tiled data deliveries and clipped data samples. The logic associated with the 
//		/firstreturn switch is much simplier and should result in consistent results no matter the processing
//		steps used to produce a point cloud file.
//
// V1.71 7/14/2009
//		Fixed problem reading list files with a blank line at the end of the file. Previous version would hang.
//
// V1.80 7/28/2009
//		Added /relcover switch for computing cover above various height metrics
//
// V1.90 8/8/2009
//		Added /alldensity switch to compute cover estimates using all returns instead of only first returns
//
// V2.0 9/16/2009 & 10/19/2009
//		Modified the logic used to compute metrics related to cover. Now the /above:# switch also triggers calculation 
//		of the proportion of returns above the mean and mode elevations (or heights) and the cover value
//		using all returns (instead of onlyfirst returns).
//
//		Also added columns for the number of returns by return number and the point counts used for the cover
//		calculations. These columns come just after the total number of points used for the metrics (pts above htmin)
//
//		Cleaned up variables computed and output
//			removed median and duplicate 50th percentile columns (median is the same as 50th percentile and this value
//				was output 3 times in the file)
//			added coefficient of variation
//			re-ordered percentile values...moved 25th and 75th percentile values into sequence of other percentile values
//
//		Also added /first switch (deos the same thing as /firstreturn switch) to maintain consistency with GridMetrics
//
// 2/3/2010 CloudMetrics V2.10
//		Added code to compute L moments and moment ratios. Added 14 columns to output (7 each for elevation and intensity values)
//
// 2/19/2010 CloudMetrics V2.20
//		Changed /htmin to /minht to match GridMetrics. /htmin is still recognized but not documented. Added /maxht
//		option to better match GridMetrics. In practice, it is best to use this option when clipping data samples with
//		ClipData and then simply use CloudMetrics to compute the metrics. However, some analysis processes are better served
//		when you can use this option with CloudMetrics (e.g. computing metrics using height slices via /outlier)
//
// 3/11/2010 CloudMetrics V2.30
//		Added file title to output as a separate column from the filename. This helps when the point data files are named
//		using a meaningful name (such as plot identifier). The /id option also provides an identifier for each line of data
//		but only works with numbers in the file name. This new column contains the full file name.
//
// 9/7/2010 CloudMetrics V2.31
//		Corrected the use of the height minimum and maximum to include points above (but not equal to) the minimum height
//		and below (but not equal to) the maximum height. Previous versions included points equal to the min and max heights.
//
// 11/2/2010 CloudMetrics V2.32
//		Added the /outlier switch to make the command line sytnax more consistent with GridMetrics. The /outlier:low,hig
//		switch has the same effect as using both the /minht:low and /maxht:high switches. The values specified via the /outlier
//		switch will override the values specified with the /minht and /maxht switches.
//
// 11/10/2010 CloudMetrics V2.33
//	Added command line parameter verification logic.
//
// 12/6/2010 CloudMetrics V2.34
//	Corrected some variable names to match those output in GridMetrics: 
//		Elev InterquartileDistance changed to Elev IQ
//		Int InterquartileDistance changed to Int IQ
//	Changed the capitalization of some of the variable names. For the most part the rule is that the first letter of the column
//	heading is capitalized and no other letter. If a word in the heading is an abbreviation, it is capitalized. For example, CV for
//	coefficient of variation.
//
//	1/14/2011 CloudMetrics V2.35
//	Restructured code to provide better organization and make it easier to maintain and distribute FUSION source code.
//
//	Added new options: /strata:[#,#,#,...] /intstrata:[#,#,#,...] /kde:[window, multiplier]
//
//	Added new metrics: Canopy relief ratio, height strata (elevation and intensity metrics), MAD_MED, MAD_MODE, #modes
//	from a kernal density function using the return heights, min/max mode values, min/max range
//
//	5/4/2011 CloudMetrics V2.36
//	Added proportion of returns in strata when using the /strata option. Also corrected a minor error in the output of 
//	intensity metrics for the strate
//
//	1/24/2012 CloudMetrics V2.37...testing complete bump version to 2.38 for post-FUSION 3.01 release
//	Experimental version:
//	Added 2 new height metrics: 
//		SQRT of the average squared point heights
//		CUBERT of the average cubed point heights
//
//	2/29/2012 CloudMetrics V2.38
//	made additional mean variables (quadratic and cubed) official
//
//	3/26/2012 CloudMetrics V2.39
//	added /rgb switch to compute intensity metrics using red, green, or blue color values from LAS version 1.2+ files
//	with RGB values for each return (point record formats 2, 3, & 5). LAS version is not actually checked because
//	some LAS writers do not correctly output the format version.
//
//	Use caution when using the /RGB switch since the output columns for the Intensity, R, G, and B metrics are the same. It
//	is very easy to mix outputs from runs intended to compute the metrics using the color values with each other and those
//	where the actual intensity value was used to compute metrics.
//
//	8/22/2012 CloudMetrics V2.40
//	Added support for LASLIB to read LAS and LAZ files. LASLIB replaces the original code I developed to read LAS files. The
//	change results in slightly faster file reads for uncompressed files and adds support for compressed LAS (LAZ) data. 
//	Other formats are still read using my original code.
//
//	Problem with LASlib code: you must link to static MFC library which increases the executable size by 10x. Also trouble
//	writing LAS files in ClipData so I am putting the use of LASlib on hold until I have time to resolve some of these issues.
//
//	10/24/2012 CloudMetrics V2.50
//	Modified the logic used for outlier removal so it does not negate the effect of /minht and /maxht options. Outlier removal 
//	is now treated independently from /minht and /maxht. You can still specify options that in incompatible. For example,
//	/minht:2.0 and /outlier:3,100 will omit returns with heights from 2.0 - 3.0.
//
//  11/16/2012 CloudMetrics V2.51
//	Corrected a problem where metrics for samples with very few points (too few to compute most metrics but at least 1 point)
//	contained an extra column of data with a value of 0.0.
//
//	1/18/2013 CloudMetrics V2.52
//	Corrected a problem with output when using the /firstreturn and /minht options. The "Total return count" in the output
//	was the count of all first returns instead of all returns. Also changed the label on the number of returns above the 
//	/minht threshold to reflect whether or not only first returns were used.
//
//	8/15/2013 CloudMetrics V2.53
//	Added support for LAZ format using the LASzip.dll from Martin Isenburg -- Rapidlasso. If the LASzip.dll is in the FUSION install
//	folder, it will be loaded and used providing support for LAS and LAZ formats. If the dll is not available, the old FUSION-based code will
//	be used to support LAS only...LAZ is not supported without the library.
//
//	12/2/2015 CloudMetrics V2.54
//	Corrected a potential problem if you used the /rgb option with input files in LDA format. Unlikely that this would cause problems but I 
//	fixed some similar issues in GridMetrics so they needed fixed in CloudMetrics as well.
//
//	8/9/2017 CloudMetrics V2.55
//	Added error message when the output file can't be opened. This was an oversight that should have been corrected long ago.
//
//	Changed the error message when a file has no points (either empty or no points above height threshold). Added a message when a file
//	doesn't have enough points to compute metrics.
//
//	Added the /rid switch to parse an identifier from the end of the file name. This was needed to deal with complex file names where
//	there were several sets of numbers in the file name but the last set of numbers contained the desired identifier.
//
//	5/18/2018 CloudMetrics V2.56
//	Added the /ignoreoverlap switch to control use of the points flagged as overlap point in LAS V1.4+ format files.
//
//	8/6/2018 CloudMetrics V2.57
//	Corrected a problem when determining the min/max values for X, Y & Z in data files. Variables for the max values were being initialized to a vary 
//	small positive number. This worked fine unless the coordinates were all negative. For negative coordinates, the maximum values reported were always 0.0.
//
//	For some programs, there was no problem but code was changed to provide consistent initial values when determining min/max values.
//
//	9/4/2018 CloudMetrics V2.60
//	Major code changes to move to "modern" development environment (MS Visual Studio 2017). Lots of small code changes needed to change environments and prepare for 
//	64-bit version.
//
//	4/30/2019 CloudMetrics V2.61
//	Changed the logic used to compute the mode intensity/elevation/height value. CloudMetrics could crash when all points in the cell have the same intensity/elevation/height. For lidar
//	data this was rare (maybe never) but for DAP-derived point clouds, this can happen. This may have also been a problem with very sample sizes where the number of 
//	points in the sample is small.
//
//	5/22/2019 CloudMetrics V2.70
//	Added a new metric: profile area. For details, see A simple and integrated approach for fire severity assessment using bi-temporal airborne LiDAR data, 
//	Int J.Appl Earth Obs Geoinforamtion, 78 (2019) : 25 - 38. In FUSION's implementation, percentile height values are normalized using the 99th percentile
//	height instead of the maximum height as in the paper. This was done to prevent problems when high outliers are present. In addition, the area is computed
//	using the percentile values (1% breaks) instead of fitting a polynomial to the data. In testing, I found very little difference between the areas computed
//	directly and those computed from a fitted curve and there are problems determining the best polynomial form (order) that works across a variety of structure
//	types.
//
//	Changed the code that computes metrics for strata so it always computes the strata metrics...even when there are too few points for other metrics. Previous
//	versions did not compute the height or intensity metrics for strata if there were too few points above the height threshold for other metrics. This was really an error
//	because all points are used for the strata metrics. In the code, the strata metrics were being computed but not reported when there were too few points for other
//	metrics. This also resulted in a difference in behavior and outputs when compared to GridMetrics.
//
//	2/25/2020 CloudMetrics V2.71
//	Changed the logic used to compute the mode elevation and intensity values for strata. This is the same changed done in V2.61 to detect and prevent a divide-by-zero
//	error but I missed it for the strata calculations. The real problem was that the min/max values for the strata can be the same so their difference is zero. This
//	can happen if there is only 1 point in the strata or if the min/max just happen to be the same.
//
//	3/9/2020 CloudMetrics V2.72
//	Changed code to move arrays used to compute strata metrics into heap memory and out of the stack. There was too much memory being allocated on the stack. I wasn't seeing
//	any problems due to this but decided to change things to prevent the possibility of problems.
//
//	4/22/2020 CloudMetrics V2.73
//	Made a change to the logic for the /pa option so percentile output is appended to an existing file. Previous versions were creating a new file without headers (column labels)
//	when the /new option was not used.
//
// 5/1/2020 CloudMetrics V2.74
//	Added code to prevent problems with the calculation of profile area when points have negative heights or the 99th percentile is negative. Changes should
//	prevent large negative and positive values for profile area.
//
// 5/17/2022 CloudMetrics V2.75
// Added logic that checks for valid point file formats and eliminates non-point files prior to the start of processing. This logic is designed to overcome problems
// with Windows and wildcard processing. For example, *.las will match files with the .las extension but also match files with the .lasx extension. This new logic
// can be turned off using the /skipfilechack option.
//
// 8/12/2024 CloudMetrics V2.76
//	Fixed the logic associated with the /rgb option to work correctly with LAS V1.4 files containing RGB information. Also added support for the NIR channel. Option
//	uses "N" to select the NIR channel for metrics.
//
#include "stdafx.h"
#include <time.h>
#include <float.h>
#include <math.h>
#include "..\..\fusion\versionID.h"
#include "lidardata_laslib.h"
#include "filespec.h"
#include "plansdtm.h"
#include "argslib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "DataCatalogEntry.h"
#include "cloudmetrics.h"
#include "command_line_core_functions.h"

#define		PROGRAM_NAME		"CloudMetrics"
#define		PROGRAM_VERSION		2.76
#define		MINIMUM_CL_PARAMS	2
//			                              1         2         3         4         5         6         7         8         9         10
//			                     1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
#define		PROGRAM_DESCRIPTION	"Computes metrics describing an entire point cloud"

#define		NUMBEROFBINS		64
#define		NUMBER_OF_CLASS_CODES		32
#define		MAX_NUMBER_OF_STRATA		32

// values used when computing matrics using RGB color values from LAS version 1.2 and newer
// used with m_RGBColor
#define		RGB_RED			1
#define		RGB_GREEN		2
#define		RGB_BLUE		3
#define		RGB_NIR			4

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// The one and only application object

CWinApp theApp;

using namespace std;

typedef struct {
	float Elevation;
	float Intensity;
	int ReturnNumber;
} STRATAPOINT;

char* ValidCommandLineSwitches = "rgb above new firstinpulse firstreturn first highpoint subset id rid minht maxht outlier strata intstrata kde ignoreoverlap pa ";

// global variables...not the best programming practice but helps with a "standard" template for command line utilities
CList<CDataCatalogEntry, CDataCatalogEntry&> DataFile;
int DataFileCount;
CDataCatalogEntry ce;
BOOL NewOutputFile;
BOOL ParseID;
BOOL ReverseParseID;
BOOL ComputeCover;
BOOL m_ProduceHighpointOutput;
BOOL m_ProduceYZLiOutput;
BOOL m_UseFirstReturnInPulse;
BOOL m_UseFirstReturns;
BOOL m_UseHeightMin;
BOOL m_UseHeightMax;

BOOL m_ComputeRelCover;
BOOL m_UseAllReturnsForCover;
BOOL m_CountReturns;

BOOL m_DoKDEStats;
double m_KDEBandwidthMultiplier;
double m_KDEWindowSize;

BOOL m_DoRGBMetrics;
BOOL m_DoNIRMetrics;
int m_RGBColor;

BOOL m_EliminateOutliers;
double m_OutlierMinHt;
double m_OutlierMaxHt;

BOOL m_SkipOverlapPoints;

BOOL m_OutputPercentileData;
BOOL m_ComputePA;

BOOL m_DoHeightStrata;
BOOL m_DoHeightStrataIntensity;
int m_HeightStrataCount;
int m_HeightStrataIntensityCount;
double m_HeightStrata[MAX_NUMBER_OF_STRATA];
double m_HeightStrataIntensity[MAX_NUMBER_OF_STRATA];

double m_MinHeight;
double m_MaxHeight;
double CoverCutoff;
CString InputFileCL;
CString OutputFileCL;
CString PercentileOutputFileCL;
CFileSpec DataFS;
CFileSpec OutputFS;
CFileSpec PercentileOutputFS;

// global functions that are modified to create a new program
void GaussianKDE(float* PointData, int Pts, double BW, double SmoothWindow, int& ModeCount, double& MinMode, double& MaxMode);

void inplace_reverse(char * str)
{
	if (str) {
		char* end = str + strlen(str) - 1;

		// swap the values in the two given variables
		// XXX: fails when a and b refer to same memory location
		while (str < end) {
			do { *str ^= *end; *end ^= *str; *str ^= *end; } while (0);
			str++;
			end--;
		}
	}
}

void usage()
{
	LTKCL_PrintProgramHeader();
	LTKCL_PrintProgramDescription();
//			         1         2         3         4         5         6         7         8         9         10
//			1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
	printf("\nSyntax: %s [switches] InputSpecifier OutputFile\n", PROGRAM_NAME);
	printf(" InputSpecifier LIDAR data file template, name of text file containing a\n");
	printf("              list of file names (must have .txt extension), or a\n");
	printf("              catalog file\n");
	printf(" OutputFile   Name for output file to contain cloud metrics (usually\n");
	printf("              .csv extension)\n");
	printf(" Switches:\n");

	LTKCL_PrintStandardSwitchInfo();
	
	printf("  above:#     Compute proportion of first returns above # (canopy cover).\n");
	printf("              Also compute the proportion of all returns above # and the\n");
	printf("              (number of returns above #) / (total number of 1st returns).\n");
	printf("              The same metrics are also computed using the mean and mode\n");
	printf("              point elevation (or height) values. Starting with version\n");
	printf("              2.0 of %s, the /relcover and /alldensity options were\n", PROGRAM_NAME);
	printf("              removed. All cover metrics are computed when the /above:#\n");
	printf("              switch is used.\n");
	printf("  new         Start new output file...delete existing output file\n");
	printf("  firstinpulse Use only the first return for the pulse to compute metrics\n");
	printf("  firstreturn Use only first returns to compute metrics\n");
	printf("  first       Use only first returns to compute metrics (same as /firstreturn)\n");
	printf("  highpoint   Produce a limited set of metrics ([ID],#pts,highX,highY,highZ)\n");
	printf("  subset      Produce a limited set of metrics([ID],#pts,Mean ht,Std dev ht,\n");
	printf("              75th percentile,cover)...must be used with /above:#\n");
	printf("  id          Parse an identifier from the beginning of the data file name...\n");
	printf("              output as the first column of data\n");
	printf("  rid         Parse an identifier from the end of the data file name...\n");
	printf("              output as the first column of data\n");
	printf("  pa          Output detailed percentile data used to compute the canopy\n");
	printf("              profile area. Output file name uses the base output name with\n");
	printf("              _percentile appended.\n");
	printf("  minht:#     Use only returns above # (use when data is normalized to ground)\n");
	printf("              Prior to version 2.20 this switch was /htmin. /htmin can still\n");
	printf("              be used but /minht is preferred. The minht is not used when\n");
	printf("              computing metrics related to the /strata and /intstrata options.\n");
	printf("  maxht:#     Use only returns below # (use when data is normalized to ground)\n");
	printf("              to compute metrics. The maxht is not used when computing metrics\n");
	printf("              related to the /strata or /intstrata options.\n");
	printf("  outlier:low,high Omit points with elevations below low and above high.\n");
	printf("              When used with data that has been normalized using a ground\n");
	printf("              surface, low and high are interpreted as heights above ground.\n");
	printf("              You should use care when using /outlier:low,high with /minht and\n");
	printf("              /maxht options. If the low value specified with /outlier is above\n");
	printf("              the value specified with /minht, the value for /outlier will\n");
	printf("              override the value specified for /minht. Similarly, if the high\n");
	printf("              value specified with /outlier is less than the value specified\n");
	printf("              for /maxht, the /outlier value will override the value for\n");
	printf("              /maxht.\n");
	printf("  ignoreoverlap Ignore points with the overlap flag set (LAS V1.4+ format)\n");
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
	printf("  rgb:color   Compute intensity metrics using the color value from the RGB\n");
	printf("              color for the returns. Valid with LAS version 1.2 and newer\n");
	printf("              data files that contain RGB information for each return (point\n");
	printf("              record types 2, 3 and 7). Valid color values are R, G, B, or N.\n");
	printf("              N (for NIR) is only valid for LAS 1.4+ files with point record\n");
	printf("              formats 8 and 10).\n");
	//	printf("  alldensity  Use all returns to compute canopy and relative cover values.\n");
//	printf("  relcover    Compute proportion of first returns above the mean and mode\n");
//	printf("              of return elevations (or heights)\n");
	printf("\nColumn labels will only be correct if the output file does not exist or if\n");
	printf("the /new switch is specified. Appending output from runs with different\n");
	printf("options will result in misaligned columns.\n");
	printf("When using /first, /firstreturn, or /firstinpulse, column headings do not\n");
	printf("indicate that only first returns were used so it is up to the user to\n");
	printf("keep track of the returns used to compute the metrics.\n");

//			         1         2         3         4         5         6         7         8         9         10
//			1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890

//	LTKCL_LaunchHelpPage();
}

void InitializeGlobalVariables()
{
	m_nRetCode = 0;
	CoverCutoff = 0.0;
	ParseID = FALSE;
	ReverseParseID = FALSE;

	m_ProduceHighpointOutput = FALSE;
	m_ProduceYZLiOutput = FALSE;
	m_UseFirstReturnInPulse = FALSE;
	m_UseFirstReturns = FALSE;
	m_UseHeightMin = FALSE;
	m_UseHeightMax = FALSE;

	m_EliminateOutliers = FALSE;

	m_UseAllReturnsForCover = FALSE;

	m_ComputeRelCover = FALSE;

	m_DoRGBMetrics = FALSE;
	m_DoNIRMetrics = FALSE;

	m_DoKDEStats = FALSE;

	m_SkipOverlapPoints = FALSE;

	m_DoHeightStrata = FALSE;
	m_DoHeightStrataIntensity = FALSE;
	m_HeightStrataCount = 0;
	m_HeightStrataIntensityCount = 0;

	m_OutputPercentileData = FALSE;
	m_ComputePA = TRUE;

	m_CountReturns = TRUE;		// always put out counts (new in V2.0)
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
	if (m_clp.CheckHelp() || m_clp.ParamCount() - m_clp.SwitchCount() < MINIMUM_CL_PARAMS) {
		usage();
		return(1);
	}

	// process command line switches
	NewOutputFile = m_clp.Switch("new");
	m_UseFirstReturnInPulse = m_clp.Switch("firstinpulse");
	m_UseFirstReturns = m_clp.Switch("firstreturn");
	if (!m_UseFirstReturns)
		m_UseFirstReturns = m_clp.Switch("first");
	ParseID = m_clp.Switch("id");
	ReverseParseID = m_clp.Switch("rid");

	m_OutputPercentileData = m_clp.Switch("pa");

	m_SkipOverlapPoints = m_clp.Switch("ignoreoverlap");

	ComputeCover = m_clp.Switch("above");
//	m_UseAllReturnsForCover = m_clp.Switch("alldensity");
	CoverCutoff = atof(m_clp.GetSwitchStr("above", "0.0"));
//	m_ComputeRelCover = m_clp.Switch("relcover");
	m_ProduceHighpointOutput = m_clp.Switch("highpoint");
	m_ProduceYZLiOutput = m_clp.Switch("subset");
	m_UseHeightMin = m_clp.Switch("htmin");
	m_MinHeight = atof(m_clp.GetSwitchStr("htmin", "0.0"));
	if (!m_UseHeightMin) {
		m_UseHeightMin = m_clp.Switch("minht");
		m_MinHeight = atof(m_clp.GetSwitchStr("minht", "0.0"));
	}
	m_UseHeightMax = m_clp.Switch("maxht");
	m_MaxHeight = atof(m_clp.GetSwitchStr("maxht", "0.0"));
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
				m_HeightStrataCount ++;

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
			m_HeightStrataCount ++;
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
				m_HeightStrataIntensityCount ++;
				
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
			m_HeightStrataIntensityCount ++;
		}
		else {
			// default
			m_HeightStrataIntensityCount = 3;
			m_HeightStrataIntensity[0] = 0.15;
			m_HeightStrataIntensity[1] = 1.37;
			m_HeightStrataIntensity[2] = DBL_MAX;
		}
	}

	m_DoRGBMetrics = m_clp.Switch("rgb");
	if (m_DoRGBMetrics) {
		// get the color value R, G, or B (no case)
		CString temp = m_clp.GetSwitchStr("rgb", "");
		if (temp.IsEmpty()) {
			m_DoRGBMetrics = FALSE;
			LTKCL_PrintStatus("No color specified for /RGB switch");
			return(19);
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
				CString csTemp;
				csTemp.Format("Invalid color option for /RGB switch: %s", (LPCSTR) temp);
				LTKCL_PrintStatus(csTemp);
				return(18);
			}
		}
	}

	// check for /outlier switch...same effect as including /htmin and htmax switches
	if (m_clp.Switch("outlier")) {
		CString temp = m_clp.GetSwitchStr("outlier", "");
		if (!temp.IsEmpty()) {
			sscanf(temp, "%lf,%lf", &m_OutlierMinHt, &m_OutlierMaxHt);

//			m_UseHeightMax = TRUE;
//			m_UseHeightMin = TRUE;
			m_EliminateOutliers = TRUE;
		}
	}

	// force all return cover metrics if doing any cover metrics
	m_UseAllReturnsForCover = ComputeCover;
	m_ComputeRelCover = ComputeCover;

	// get input file specifier...may be single file, wildcard, or list file with .txt extension
	InputFileCL = m_clp.ParamStr(m_clp.FirstNonSwitchIndex());
	DataFS.SetFullSpec(InputFileCL);

	// get output file specifier
	OutputFileCL = m_clp.ParamStr(m_clp.FirstNonSwitchIndex() + 1);

	// check to see if the output file exists...if not, set the NewOutputFile flag to include headers
	// in the output file
	OutputFS.SetFullSpec(OutputFileCL);
	if (!OutputFS.Exists())
		NewOutputFile = TRUE;

	// set file specifier and file name for percentile data...append "_percentile" to file name
	PercentileOutputFS.SetFullSpec(OutputFileCL);
	CString title = PercentileOutputFS.FileTitle();
	title += _T("_percentile");
	PercentileOutputFS.SetTitle(title);
	PercentileOutputFileCL = PercentileOutputFS.GetFullSpec();

	// build list of input data files...may contain 1 or more files
	if (DataFS.Extension().CompareNoCase(".txt") == 0) {
		// read data files from list file
		CDataFile lst(DataFS.GetFullSpec());
		if (lst.IsValid()) {
			char buf[1024];
			CFileSpec TempFS;
			while (lst.NewReadASCIILine(buf)) {
				TempFS.SetFullSpec(buf);
				if (TempFS.Exists()) {
					ce.m_FileName = TempFS.GetFullSpec();
					ce.m_CheckSum = 0;
					ce.m_MinX = ce.m_MinY = ce.m_MinZ = ce.m_MaxX = ce.m_MaxY = ce.m_MaxZ = 0.0;
					ce.m_PointDensity= -1.0;
					ce.m_Points = -1;
					DataFile.AddTail(ce);

					DataFileCount ++;
				}
			}
		}
	}
	else if (DataFS.Extension().CompareNoCase(".csv") == 0) {
		// read data files from catalog file
		CDataFile lst(DataFS.GetFullSpec());
		if (lst.IsValid()) {
			char buf[1024];
			CFileSpec TempFS;
			WIN32_FILE_ATTRIBUTE_DATA attribdata;
			long chksum;
			while (lst.ReadASCIILine(buf)) {
				// parse file name and check to see if it exists
				TempFS.SetFullSpec(strtok(buf, " ,\t"));
				if (TempFS.Exists()) {
					ce.m_FileName = TempFS.GetFullSpec();

					// parse remaining fields
					ce.m_CheckSum = atol(strtok(NULL, " ,\t"));
					ce.m_MinX = atof(strtok(NULL, " ,\t"));
					ce.m_MinY = atof(strtok(NULL, " ,\t"));
					ce.m_MinZ = atof(strtok(NULL, " ,\t"));
					ce.m_MaxX = atof(strtok(NULL, " ,\t"));
					ce.m_MaxY = atof(strtok(NULL, " ,\t"));
					ce.m_MaxZ = atof(strtok(NULL, " ,\t"));
					ce.m_PointDensity = atof(strtok(NULL, " ,\t"));
					ce.m_Points = atoi(strtok(NULL, " ,\t"));

					// validate the checksum
					// get file modify time and compute checksum value
					// using low DWORD components should catch all the details.  Not likely the modification time will keep the 
					// same nanosecond count or that the file size will change by 2Gb chunks
					chksum = 0;
					if (GetFileAttributesEx(ce.m_FileName, GetFileExInfoStandard, &attribdata)) {
						chksum = attribdata.nFileSizeLow + attribdata.ftLastWriteTime.dwLowDateTime;
					}

					// if checksum doesn't match clear the information from the catalog entry
					if (ce.m_CheckSum != chksum) {
						ce.m_CheckSum = 0;
						ce.m_MinX = ce.m_MinY = ce.m_MinZ = ce.m_MaxX = ce.m_MaxY = ce.m_MaxZ = 0.0;
						ce.m_PointDensity= -1.0;
						ce.m_Points = -1;
					}

					DataFile.AddTail(ce);
					DataFileCount ++;
				}
			}
		}
	}
	else {
		// look for data files using file specifier
		// 2/24/2020 Looking for problem when processing output clips from TreeSeg. There can be 40,000+ files and the behavior is that
		// we get metrics for a handful of clips, Cloudmetrics fails without an error, and he last row in the CSV output is truncated
		// possible problem is not checking bWorking returned from FindFileNext(). If 0, call GetLastError and verify that the return value
		// is ERROR_NO_MORE_FILES before processing the last "good" file. Any other error return value is an error
		CFileFind finder;
		int FileCount = 0;
		BOOL bWorking = finder.FindFile(InputFileCL);
		while (bWorking) {
			bWorking = finder.FindNextFile();
			FileCount ++;
		}

		if (FileCount > 1 || (FileCount == 1 && InputFileCL.FindOneOf("*?") >= 0)) {
			bWorking = finder.FindFile(InputFileCL);
			DataFileCount = 0;
			while (bWorking) {
				bWorking = finder.FindNextFile();
				ce.m_FileName = finder.GetFilePath();
				ce.m_CheckSum = 0;
				ce.m_MinX = ce.m_MinY = ce.m_MinZ = ce.m_MaxX = ce.m_MaxY = ce.m_MaxZ = 0.0;
				ce.m_PointDensity= -1.0;
				ce.m_Points = -1;
				DataFile.AddTail(ce);

				DataFileCount ++;
			}
		}
		else {
			ce.m_FileName = InputFileCL;
			ce.m_CheckSum = 0;
			ce.m_MinX = ce.m_MinY = ce.m_MinZ = ce.m_MaxX = ce.m_MaxY = ce.m_MaxZ = 0.0;
			ce.m_PointDensity= -1.0;
			ce.m_Points = -1;
			DataFile.AddTail(ce);

			DataFileCount = 1;
		}
	}

	if (m_ProduceYZLiOutput && !ComputeCover) {
		LTKCL_PrintStatus("You must use the /above:# switch with the /subset switch");
		return(1);
	}
	return(0);
}

// comparison function for sorting...must be global to use qsort()
int compareflt(const void *arg1, const void *arg2)
{
	if (*((float*) arg1) < (*(float*) arg2))
		return(-1);
	else if (*((float*) arg1) > (*(float*) arg2))
		return(1);
	else
		return(0);
}

// comparison function for sorting...must be global to use qsort()
int compareSP(const void *arg1, const void *arg2)
{
	if (((STRATAPOINT*) arg1)->Elevation < (((STRATAPOINT*) arg2)->Elevation))
		return(-1);
	else if (((STRATAPOINT*) arg1)->Elevation > (((STRATAPOINT*) arg2)->Elevation))
		return(1);
	else
		return(0);
}

// comparison function for sorting...must be global to use qsort()
int compareSPint(const void *arg1, const void *arg2)
{
	if (((STRATAPOINT*) arg1)->Intensity < (((STRATAPOINT*) arg2)->Intensity))
		return(-1);
	else if (((STRATAPOINT*) arg1)->Intensity > (((STRATAPOINT*) arg2)->Intensity))
		return(1);
	else
		return(0);
}

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	CString csTemp;

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
		if (!LTKCL_Initialize())
			return(0);

		InitializeGlobalVariables();

		m_nRetCode = ParseCommandLine();

		m_nRetCode |= PresentInteractiveDialog();

		// attempt to load LAZ/LAS dll
		if (!m_NoLASzip_DLL)
			laszip_load_dll();

		// do the processing to create data subsample
		// ********************************************************************************************************
		if (DataFileCount && !m_nRetCode) {
			// print status info
			LTKCL_PrintProgramHeader();
			LTKCL_PrintCommandLine();
			LTKCL_PrintRunTime();

			// look for bad switches on command line
			LTKCL_VerifyCommandLine(ValidCommandLineSwitches);
			
			if (!m_nRetCode) {
				BOOL LASHasRGB = TRUE;
				BOOL LASHasNIR = TRUE;

				// if doing RGB metrics, check for LAS 1.2+ and RGB point records
				if (m_DoRGBMetrics) {
					CLidarData dat(ce.m_FileName);
					if (dat.IsValid()) {
						if (dat.GetFileFormat() != LASDATA) {
							m_DoRGBMetrics = FALSE;
							LASHasNIR = FALSE;
							LASHasRGB = FALSE;
						}
						else {
#ifdef USE_LASLIB
							if (dat.lasreader->point.have_rgb) {
								LASHasRGB = FALSE;
							}
#else
							if (dat.m_HaveLASZIP_DLL) {
								if (dat.lasdll_header->point_data_format != 2 &&
									dat.lasdll_header->point_data_format != 3 &&
									dat.lasdll_header->point_data_format != 5 &&
									dat.lasdll_header->point_data_format != 7 &&
									dat.lasdll_header->point_data_format != 8 &&
									dat.lasdll_header->point_data_format != 10
									) {
									LASHasRGB = FALSE;
									LASHasNIR = FALSE;
								}

								if (m_DoNIRMetrics && dat.lasdll_header->point_data_format != 8 && dat.lasdll_header->point_data_format != 10)
									LASHasNIR = FALSE;
							}
							else {
								if (dat.m_LASFile.Header.PointDataFormatID != 2 &&
									dat.m_LASFile.Header.PointDataFormatID != 3 &&
									dat.m_LASFile.Header.PointDataFormatID != 5 &&
									dat.m_LASFile.Header.PointDataFormatID != 7 &&
									dat.m_LASFile.Header.PointDataFormatID != 8 &&
									dat.m_LASFile.Header.PointDataFormatID != 10
									) {
									LASHasRGB = FALSE;
									LASHasNIR = FALSE;
								}
								if (m_DoNIRMetrics && dat.m_LASFile.Header.PointDataFormatID != 8 && dat.m_LASFile.Header.PointDataFormatID != 10)
									LASHasNIR = FALSE;
							}
#endif
						}
					}
					if (m_DoRGBMetrics & !LASHasRGB) {
						LTKCL_PrintStatus("To use the /RGB switch, data files must be LAS version 1.2 or newer and contain RGB color values for each return.\n(Point data record formats 2, 3, 5, 7, 8, or 10).\nSome of the data files do not meet this requirement.");

						m_nRetCode = 20;
					}

					if (m_DoNIRMetrics & !LASHasNIR) {
						LTKCL_PrintStatus("To use the /RGB switch with NIR values, data files must be LAS version 1.4 or newer and contain NIR values for each return.\n(Point data record formats 8 or 10).\nSome of the data files do not meet this requirement.");

						m_nRetCode = 20;
					}
				}
			}

			if (!m_nRetCode) {
				CFileSpec Tempfs;
				CFileSpec TempfsForFileTitle;
				LIDARRETURN pt;
				int i, j, k, l;

				// stat variables
				int PointCount;
				int TotalPointCount;
				int TempPointCount;
				int StrataPointCount;
				double ElevMin, ElevMax, ElevMean, ElevMedian, ElevMode, ElevStdDev, ElevVariance, ElevIQDist, ElevSkewness, ElevKurtosis, ElevAAD, ElevP25, ElevP75;
				double ElevP01, ElevP05, ElevP10, ElevP20, ElevP30, ElevP40, ElevP50, ElevP60, ElevP70, ElevP80, ElevP90, ElevP95, ElevP99;
				double ElevL1, ElevL2, ElevL3, ElevL4;
				double ElevSumSquare, ElevSumCube;
				double IntMin, IntMax, IntMean, IntMedian, IntMode, IntStdDev, IntVariance, IntIQDist, IntSkewness, IntKurtosis, IntAAD, IntP25, IntP75;
				double IntP01, IntP05, IntP10, IntP20, IntP30, IntP40, IntP50, IntP60, IntP70, IntP80, IntP90, IntP95, IntP99;
				double IntL1, IntL2, IntL3, IntL4;
				double Cover;
				double AllCover;
				double AllFirstCover;
				double ProfileArea;
				double HighX, HighY, HighElevation;

				double CanopyReliefRatio, ElevMadMedian, ElevMadMode;
				int KDE_ModeCount;
				double KDE_MaxMode, KDE_MinMode, KDE_ModeRange;

				// 3/9/2020 move arrays into heap
				int* ElevStrataCount = new int[MAX_NUMBER_OF_STRATA];
				int ElevStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
				double* ElevStrataMean = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataMin = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataMax = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataMedian = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataMode = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataSkewness = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataKurtosis = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataVariance = new double[MAX_NUMBER_OF_STRATA];
				double* ElevStrataM2 = new double[MAX_NUMBER_OF_STRATA];		// used to compute variance
				double* ElevStrataM3 = new double[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				double* ElevStrataM4 = new double[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis

				int* IntStrataCount = new int[MAX_NUMBER_OF_STRATA];
				int IntStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
				double* IntStrataMean = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataMin = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataMax = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataMedian = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataMode = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataSkewness = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataKurtosis = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataVariance = new double[MAX_NUMBER_OF_STRATA];
				double* IntStrataM2 = new double[MAX_NUMBER_OF_STRATA];		// used to compute variance
				double* IntStrataM3 = new double[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				double* IntStrataM4 = new double[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis

				//int ElevStrataCount[MAX_NUMBER_OF_STRATA];
				//int ElevStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
				//double ElevStrataMean[MAX_NUMBER_OF_STRATA];
				//double ElevStrataMin[MAX_NUMBER_OF_STRATA];
				//double ElevStrataMax[MAX_NUMBER_OF_STRATA];
				//double ElevStrataMedian[MAX_NUMBER_OF_STRATA];
				//double ElevStrataMode[MAX_NUMBER_OF_STRATA];
				//double ElevStrataSkewness[MAX_NUMBER_OF_STRATA];
				//double ElevStrataKurtosis[MAX_NUMBER_OF_STRATA];
				//double ElevStrataVariance[MAX_NUMBER_OF_STRATA];
				//double ElevStrataM2[MAX_NUMBER_OF_STRATA];		// used to compute variance
				//double ElevStrataM3[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				//double ElevStrataM4[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				//
				//int IntStrataCount[MAX_NUMBER_OF_STRATA];
				//int IntStrataCountReturn[MAX_NUMBER_OF_STRATA][11];
				//double IntStrataMean[MAX_NUMBER_OF_STRATA];
				//double IntStrataMin[MAX_NUMBER_OF_STRATA];
				//double IntStrataMax[MAX_NUMBER_OF_STRATA];
				//double IntStrataMedian[MAX_NUMBER_OF_STRATA];
				//double IntStrataMode[MAX_NUMBER_OF_STRATA];
				//double IntStrataSkewness[MAX_NUMBER_OF_STRATA];
				//double IntStrataKurtosis[MAX_NUMBER_OF_STRATA];
				//double IntStrataVariance[MAX_NUMBER_OF_STRATA];
				//double IntStrataM2[MAX_NUMBER_OF_STRATA];		// used to compute variance
				//double IntStrataM3[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				//double IntStrataM4[MAX_NUMBER_OF_STRATA];		// used to compute skewness & kurtosis
				double delta;
				double delta_n;
				double delta_n2;
				double term1;
				int n1;

				int FirstReturnsAbove;
				int FirstReturnsTotal;
				int AllReturnsAbove;
				int AllReturnsTotal;
				float* ElevValueList;
				float* IntValueList;
				STRATAPOINT* StrataPointList;
				float ElevPercentile[21];
				float IntPercentile[21];
				float SpecialElevPercentile[101];
				int Bins[NUMBEROFBINS];
				int ReturnCounts[10];
				int TheBin;
				int ID;
				int LastReturn;
				float Fraction;
				int WholePart;
				double CL1, CL2, CL3, CR1, CR2, CR3, C1, C2, C3, C4;

				// echo strata info
				// verbose status with strata...don't print the last one
				char ts[1024];
				if (m_DoHeightStrata) {
					sprintf(ts, "Elevation strata:");

					for (int i = 0; i < m_HeightStrataCount - 1; i ++) {
						sprintf(ts, "%s %.2lf", ts, m_HeightStrata[i]);
					}
					LTKCL_PrintVerboseStatus(ts);
				}


				// verbose status with strata...don't print the last one
				if (m_DoHeightStrataIntensity) {
					sprintf(ts, "Intensity strata:");

					for (int i = 0; i < m_HeightStrataIntensityCount - 1; i ++) {
						sprintf(ts, "%s %.2lf", ts, m_HeightStrataIntensity[i]);
					}
					LTKCL_PrintVerboseStatus(ts);
				}

				// open output file...if new switch was used, start a new file, otherwise append and don't write a header
				FILE* f;
				FILE* pf;
				if (NewOutputFile) {
					// open new file (overwrite existing) and output headers
					f = fopen(OutputFileCL, "wt");

					if (m_OutputPercentileData) {
						pf = fopen(PercentileOutputFileCL, "wt");

						// write header
						if (ParseID || ReverseParseID)
							fprintf(pf, "Identifier,");

						fprintf(pf, "DataFile,Elev maximum");

						for (int i = 0; i < 101; i++) {
							fprintf(pf, ",Elev P%0i", i);
						}
						fprintf(pf, ",Profile area\n");
					}

					if (ParseID || ReverseParseID)
						fprintf(f, "Identifier,");

					if (m_ProduceHighpointOutput) {
						fprintf(f, "DataFile,Points,High point X,High point Y,High point elevation\n");
					}
					else if (m_ProduceYZLiOutput) {
						fprintf(f, "DataFile,Points,Elev mean,Elev stddev,Elev P75,Percentage of first returns above %.2lf\n", CoverCutoff);
					}
					else {
						// write base column label header
						if (m_UseHeightMin) {
							if (m_UseHeightMax) {
								if (m_UseFirstReturns)
									fprintf(f, "DataFile,FileTitle,Total return count,Total first return count above %.2lf and below %.2lf", m_MinHeight, m_MaxHeight);
								else
									fprintf(f, "DataFile,FileTitle,Total return count,Total return count above %.2lf and below %.2lf", m_MinHeight, m_MaxHeight);
							}
							else {
								if (m_UseFirstReturns)
									fprintf(f, "DataFile,FileTitle,Total return count,Total first return count above %.2lf", m_MinHeight);
								else
									fprintf(f, "DataFile,FileTitle,Total return count,Total return count above %.2lf", m_MinHeight);
							}
						}
						else if (m_UseHeightMax) {
							if (m_UseFirstReturns)
								fprintf(f, "DataFile,FileTitle,Total return count,Total first return count below %.2lf", m_MaxHeight);
							else
								fprintf(f, "DataFile,FileTitle,Total return count,Total return count below %.2lf", m_MaxHeight);
						}
						else
							fprintf(f, "DataFile,FileTitle,Total return count");

						// add headings for return counts
						if (m_CountReturns) {
							if (m_UseHeightMin) {
								if (m_UseHeightMax)
									fprintf(f, ",Return 1 count above %.2lf and below %.2lf,Return 2 count above %.2lf and below %.2lf,Return 3 count above %.2lf and below %.2lf,Return 4 count above %.2lf and below %.2lf,Return 5 count above %.2lf and below %.2lf,Return 6 count above %.2lf and below %.2lf,Return 7 count above %.2lf and below %.2lf,Return 8 count above %.2lf and below %.2lf,Return 9 count above %.2lf and below %.2lf,Other return count above %.2lf and below %.2lf", m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight, m_MinHeight, m_MaxHeight);
								else
									fprintf(f, ",Return 1 count above %.2lf,Return 2 count above %.2lf,Return 3 count above %.2lf,Return 4 count above %.2lf,Return 5 count above %.2lf,Return 6 count above %.2lf,Return 7 count above %.2lf,Return 8 count above %.2lf,Return 9 count above %.2lf,Other return count above %.2lf", m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight, m_MinHeight);
							}
							else if (m_UseHeightMax)
									fprintf(f, ",Return 1 count below %.2lf,Return 2 count below %.2lf,Return 3 count below %.2lf,Return 4 count below %.2lf,Return 5 count below %.2lf,Return 6 count below %.2lf,Return 7 count below %.2lf,Return 8 count below %.2lf,Return 9 count below %.2lf,Other return count below %.2lf", m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight, m_MaxHeight);
							else
								fprintf(f, ",Return 1 count,Return 2 count,Return 3 count,Return 4 count,Return 5 count,Return 6 count,Return 7 count,Return 8 count,Return 9 count,Other return count");
						}

						fprintf(f, ",Elev minimum,Elev maximum,Elev mean,Elev mode,Elev stddev,Elev variance,Elev CV,Elev IQ,Elev skewness,Elev kurtosis,Elev AAD,Elev MAD median,Elev MAD mode,Elev L1,Elev L2,Elev L3,Elev L4,Elev L CV,Elev L skewness,Elev L kurtosis,Elev P01,Elev P05,Elev P10,Elev P20,Elev P25,Elev P30,Elev P40,Elev P50,Elev P60,Elev P70,Elev P75,Elev P80,Elev P90,Elev P95,Elev P99,Canopy relief ratio,Elev SQRT mean SQ,Elev CURT mean CUBE");
//	prior to general means					fprintf(f, ",Elev minimum,Elev maximum,Elev mean,Elev mode,Elev stddev,Elev variance,Elev CV,Elev IQ,Elev skewness,Elev kurtosis,Elev AAD,Elev MAD median,Elev MAD mode,Elev L1,Elev L2,Elev L3,Elev L4,Elev L CV,Elev L skewness,Elev L kurtosis,Elev P01,Elev P05,Elev P10,Elev P20,Elev P25,Elev P30,Elev P40,Elev P50,Elev P60,Elev P70,Elev P75,Elev P80,Elev P90,Elev P95,Elev P99,Canopy relief ratio");

						if (m_DoRGBMetrics) {
							if (m_RGBColor == RGB_RED)
								fprintf(f, ",Red minimum,Red maximum,Red mean,Red mode,Red stddev,Red variance,Red CV,Red IQ,Red skewness,Red kurtosis,Red AAD,Red L1,Red L2,Red L3,Red L4,Red L CV,Red L skewness,Red L kurtosis,Red P01,Red P05,Red P10,Red P20,Red P25,Red P30,Red P40,Red P50,Red P60,Red P70,Red P75,Red P80,Red P90,Red P95,Red P99");
							else if (m_RGBColor == RGB_GREEN)
								fprintf(f, ",Green minimum,Green maximum,Green mean,Green mode,Green stddev,Green variance,Green CV,Green IQ,Green skewness,Green kurtosis,Green AAD,Green L1,Green L2,Green L3,Green L4,Green L CV,Green L skewness,Green L kurtosis,Green P01,Green P05,Green P10,Green P20,Green P25,Green P30,Green P40,Green P50,Green P60,Green P70,Green P75,Green P80,Green P90,Green P95,Green P99");
							else if (m_RGBColor == RGB_BLUE)
								fprintf(f, ",Blue minimum,Blue maximum,Blue mean,Blue mode,Blue stddev,Blue variance,Blue CV,Blue IQ,Blue skewness,Blue kurtosis,Blue AAD,Blue L1,Blue L2,Blue L3,Blue L4,Blue L CV,Blue L skewness,Blue L kurtosis,Blue P01,Blue P05,Blue P10,Blue P20,Blue P25,Blue P30,Blue P40,Blue P50,Blue P60,Blue P70,Blue P75,Blue P80,Blue P90,Blue P95,Blue P99");
							else if (m_RGBColor == RGB_NIR)
								fprintf(f, ",NIR minimum,NIR maximum,NIR mean,NIR mode,NIR stddev,NIR variance,NIR CV,NIR IQ,NIR skewness,NIR kurtosis,NIR AAD,NIR L1,NIR L2,NIR L3,NIR L4,NIR L CV,NIR L skewness,NIR L kurtosis,NIR P01,NIR P05,NIR P10,NIR P20,NIR P25,NIR P30,NIR P40,NIR P50,NIR P60,NIR P70,NIR P75,NIR P80,NIR P90,NIR P95,NIR P99");
						}
						else
							fprintf(f, ",Int minimum,Int maximum,Int mean,Int mode,Int stddev,Int variance,Int CV,Int IQ,Int skewness,Int kurtosis,Int AAD,Int L1,Int L2,Int L3,Int L4,Int L CV,Int L skewness,Int L kurtosis,Int P01,Int P05,Int P10,Int P20,Int P25,Int P30,Int P40,Int P50,Int P60,Int P70,Int P75,Int P80,Int P90,Int P95,Int P99");

						if (ComputeCover) {
							if (m_UseAllReturnsForCover)
								fprintf(f, ",Percentage first returns above %.2lf,Percentage all returns above %.2lf,(All returns above %.2lf) / (Total first returns) * 100,First returns above %.2lf,All returns above %.2lf", CoverCutoff, CoverCutoff, CoverCutoff, CoverCutoff, CoverCutoff);
							else
								fprintf(f, ",Percentage first returns above %.2lf", CoverCutoff);
						}

						if (m_ComputeRelCover) {
							fprintf(f, ",Percentage first returns above mean,Percentage first returns above mode,Percentage all returns above mean,Percentage all returns above mode,(All returns above mean) / (Total first returns) * 100,(All returns above mode) / (Total first returns) * 100");
							fprintf(f, ",First returns above mean,First returns above mode,All returns above mean,All returns above mode,Total first returns,Total all returns");
						}

						if (m_DoKDEStats) {
							// column headings for KDE stuff
							fprintf(f, ",KDE elev modes,KDE elev min mode,KDE elev max mode,KDE elev mode range");
						}

						if (m_DoHeightStrata) {
							// print column labels...don't use last strata as it was "added" to provide upper bound
							// first strata
							fprintf(f, ",Elev strata (below %.2lf) total return count,Elev strata (below %.2lf) return proportion,Elev strata (below %.2lf) min,Elev strata (below %.2lf) max,Elev strata (below %.2lf) mean,Elev strata (below %.2lf) mode,Elev strata (below %.2lf) median,Elev strata (below %.2lf) stddev,Elev strata (below %.2lf) CV,Elev strata (below %.2lf) skewness,Elev strata (below %.2lf) kurtosis", 
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

							for (k = 1; k < m_HeightStrataCount - 1; k ++) {
								fprintf(f, ",Elev strata (%.2lf to %.2lf) total return count,Elev strata (%.2lf to %.2lf) return proportion,Elev strata (%.2lf to %.2lf) min,Elev strata (%.2lf to %.2lf) max,Elev strata (%.2lf to %.2lf) mean,Elev strata (%.2lf to %.2lf) mode,Elev strata (%.2lf to %.2lf) median,Elev strata (%.2lf to %.2lf) stddev,Elev strata (%.2lf to %.2lf) CV,Elev strata (%.2lf to %.2lf) skewness,Elev strata (%.2lf to %.2lf) kurtosis", 
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
							fprintf(f, ",Elev strata (above %.2lf) total return count,Elev strata (above %.2lf) return proportion,Elev strata (above %.2lf) min,Elev strata (above %.2lf) max,Elev strata (above %.2lf) mean,Elev strata (above %.2lf) mode,Elev strata (above %.2lf) median,Elev strata (above %.2lf) stddev,Elev strata (above %.2lf) CV,Elev strata (above %.2lf) skewness,Elev strata (above %.2lf) kurtosis", 
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
							fprintf(f, ",Int strata (below %.2lf) total return count,Int strata (below %.2lf) return proportion,Int strata (below %.2lf) min,Int strata (below %.2lf) max,Int strata (below %.2lf) mean,Int strata (below %.2lf) mode,Int strata (below %.2lf) median,Int strata (below %.2lf) stddev,Int strata (below %.2lf) CV,Int strata (below %.2lf) skewness,Int strata (below %.2lf) kurtosis", 
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

							for (k = 1; k < m_HeightStrataIntensityCount - 1; k ++) {
								fprintf(f, ",Int strata (%.2lf to %.2lf) total return count,Int strata (%.2lf to %.2lf) return proportion,Int strata (%.2lf to %.2lf) min,Int strata (%.2lf to %.2lf) max,Int strata (%.2lf to %.2lf) mean,Int strata (%.2lf to %.2lf) mode,Int strata (%.2lf to %.2lf) median,Int strata (%.2lf to %.2lf) stddev,Int strata (%.2lf to %.2lf) CV,Int strata (%.2lf to %.2lf) skewness,Int strata (%.2lf to %.2lf) kurtosis", 
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
							fprintf(f, ",Int strata (above %.2lf) total return count,Int strata (above %.2lf) return proportion,Int strata (above %.2lf) min,Int strata (above %.2lf) max,Int strata (above %.2lf) mean,Int strata (above %.2lf) mode,Int strata (above %.2lf) median,Int strata (above %.2lf) stddev,Int strata (above %.2lf) CV,Int strata (above %.2lf) skewness,Int strata (above %.2lf) kurtosis", 
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

						// print end of line for header
						fprintf(f, ",Profile area\n");
					}
				}
				else {
					// open for append
					f = fopen(OutputFileCL, "at");

					if (m_OutputPercentileData)
						pf = fopen(PercentileOutputFileCL, "at");
				}

				if (f) {
					// iterate through the list of data files and compute metrics
					POSITION pos = DataFile.GetHeadPosition();
					for (i = 0; i < DataFile.GetCount(); i++) {
						ce = DataFile.GetNext(pos);

						// 5/17/2022 check file format...skip if not valid point file
						if (!LTKCL_VerifyPointFileFormatIsValid(ce.m_FileName)) {
							CString csStatus;
							csStatus.Format("Skipping invalid point file from input list: %s", (LPCSTR) ce.m_FileName);
							LTKCL_PrintStatus(csStatus);

						continue;
						}

						PointCount = 0;
						TotalPointCount = 0;

						// intialize return counts
						for (k = 0; k < 10; k ++)
							ReturnCounts[k] = 0;

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

						ID = 0;

						CLidarData dat(ce.m_FileName);
						if (dat.IsValid()) {
							// get the file title
							TempfsForFileTitle.SetFullSpec(ce.m_FileName);

							// parse ID from file name
							if (ParseID || ReverseParseID) {
								CFileSpec tfs(ce.m_FileName);
								if (ParseID) {
									if (strpbrk(tfs.FileTitle(), "0123456789")) {
										ID = atoi(strpbrk(tfs.FileTitle(), "0123456789"));
									}
									else {
										ID = 0;
									}
								}
								else if (ReverseParseID) {
									// create a copy of the file title
									char* tc = (char*) new char[tfs.FileTitle().GetLength() + 1];
									if (tc) {
										strcpy(tc, tfs.FileTitle());

										// reverse the title
										inplace_reverse(tc);

										// find the first non-numeric character
										int nnc = (int) strspn(tc, "0123456789");

										// set end marker
										if (nnc >= 0) {
											// parse ID
											tc[nnc] = '\0';
											inplace_reverse(tc);

											ID = atoi(tc);
										}
										else
											ID = 0;

										delete [] tc;
									}
									else
										ID = 0;
								}
							}

							// initialize variables
							ElevMin = 9999999999.0;
							ElevMax = -9999999999.0;
							ElevMean = 0.0;
							IntMin = 9999999999.0;
							IntMax = -9999999999.0;
							IntMean = 0.0;
							CanopyReliefRatio = 0.0;
							ProfileArea = 0.0;
							LastReturn = 9999;
							int MaxCount = -1;

							// read all returns and do simple statistics
							while (dat.ReadNextRecord(&pt)) {
								if (dat.LastPointIsWithheld())
									continue;

								if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
									continue;

								TotalPointCount ++;

								if (m_UseFirstReturns && pt.ReturnNumber > 1) {
									LastReturn = pt.ReturnNumber;
									continue;
								}

								if (m_UseFirstReturnInPulse && (pt.ReturnNumber > 1 && pt.ReturnNumber > LastReturn)) {
									LastReturn = pt.ReturnNumber;
									continue;
								}

								if (m_UseHeightMin && pt.Elevation <= m_MinHeight) {
									LastReturn = pt.ReturnNumber;
									continue;
								}

								if (m_UseHeightMax && pt.Elevation >= m_MaxHeight) {
									LastReturn = pt.ReturnNumber;
									continue;
								}

								if (m_EliminateOutliers && (pt.Elevation >= m_OutlierMaxHt || pt.Elevation <= m_OutlierMinHt)) {
									LastReturn = pt.ReturnNumber;
									continue;
								}

								// if doing metrics with RGB values, swap point intensity for the desired value
#ifdef USE_LASLIB
								if (m_DoRGBMetrics) {
									if (m_RGBColor == RGB_RED)
										pt.Intensity = (float) dat.lasreader->point.rgb[0];
									else if (m_RGBColor == RGB_GREEN)
										pt.Intensity = (float) dat.lasreader->point.rgb[1];
									else if (m_RGBColor == RGB_BLUE)
										pt.Intensity = (float) dat.lasreader->point.rgb[2];
								}
#else
								if (dat.m_HaveLASZIP_DLL) {
									if (m_DoRGBMetrics) {
										if (m_RGBColor == RGB_RED)
											pt.Intensity = dat.lasdll_point->rgb[0];
										else if (m_RGBColor == RGB_GREEN)
											pt.Intensity = dat.lasdll_point->rgb[1];
										else if (m_RGBColor == RGB_BLUE)
											pt.Intensity = dat.lasdll_point->rgb[2];
										else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
											pt.Intensity = dat.lasdll_point->rgb[3];
									}
								}
								else {
									if (m_DoRGBMetrics) {
										if (m_RGBColor == RGB_RED)
											pt.Intensity = (float) dat.m_LASFile.PointRecord.Red;
										else if (m_RGBColor == RGB_GREEN)
											pt.Intensity = (float) dat.m_LASFile.PointRecord.Green;
										else if (m_RGBColor == RGB_BLUE)
											pt.Intensity = (float) dat.m_LASFile.PointRecord.Blue;
										else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
											pt.Intensity = (float)dat.m_LASFile.PointRecord.NIR;
									}
								}
#endif
//pt.Intensity = dat.m_LASFile.PointRecord.FileMarker;

								if (PointCount == 0) {
									HighX = pt.X;
									HighY = pt.Y;
									HighElevation = pt.Elevation;
								}

								if (pt.Elevation < ElevMin)
									ElevMin = (double) pt.Elevation;
								if (pt.Elevation > ElevMax) {
									ElevMax = (double) pt.Elevation;
									HighX = pt.X;
									HighY = pt.Y;
									HighElevation = pt.Elevation;
								}

								ElevMean += (double) pt.Elevation;

								if (pt.Intensity < IntMin)
									IntMin = (double) pt.Intensity;
								if (pt.Intensity > IntMax)
									IntMax = (double) pt.Intensity;

								IntMean += (double) pt.Intensity;

								PointCount ++;

								// count returns
								if (pt.ReturnNumber < 10 && pt.ReturnNumber >= 1)
									ReturnCounts[pt.ReturnNumber - 1] ++;
								else
									ReturnCounts[9] ++;

								LastReturn = pt.ReturnNumber;
							}

							// check the total number of points to see if we can compute metrics
							// compute strata and percentile metrics for PA using all points
							if (TotalPointCount > 0) {
								// allocate space for a list of point elevations and intensity values
								if (m_DoHeightStrata || m_DoHeightStrataIntensity || m_ComputePA) {
									StrataPointList = (STRATAPOINT*) new STRATAPOINT[TotalPointCount];

									// fill list with points
									dat.Rewind();
									LastReturn = 9999;
									StrataPointCount = 0;
									while (dat.ReadNextRecord(&pt)) {
										if (dat.LastPointIsWithheld())
											continue;

										if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
											continue;

										if (m_UseFirstReturns && pt.ReturnNumber > 1) {
											LastReturn = pt.ReturnNumber;
											continue;
										}

										if (m_UseFirstReturnInPulse && (pt.ReturnNumber > 1 && pt.ReturnNumber > LastReturn)) {
											LastReturn = pt.ReturnNumber;
											continue;
										}

										// if doing metrics with RGB values, swap point intensity for the desired value
#ifdef USE_LASLIB
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												pt.Intensity = (float)dat.lasreader->point.rgb[0];
											else if (m_RGBColor == RGB_GREEN)
												pt.Intensity = (float)dat.lasreader->point.rgb[1];
											else if (m_RGBColor == RGB_BLUE)
												pt.Intensity = (float)dat.lasreader->point.rgb[2];
										}
#else
										if (dat.m_HaveLASZIP_DLL) {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													pt.Intensity = dat.lasdll_point->rgb[0];
												else if (m_RGBColor == RGB_GREEN)
													pt.Intensity = dat.lasdll_point->rgb[1];
												else if (m_RGBColor == RGB_BLUE)
													pt.Intensity = dat.lasdll_point->rgb[2];
												else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
													pt.Intensity = dat.lasdll_point->rgb[3];
											}
										}
										else {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													pt.Intensity = (float)dat.m_LASFile.PointRecord.Red;
												else if (m_RGBColor == RGB_GREEN)
													pt.Intensity = (float)dat.m_LASFile.PointRecord.Green;
												else if (m_RGBColor == RGB_BLUE)
													pt.Intensity = (float)dat.m_LASFile.PointRecord.Blue;
												else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
													pt.Intensity = (float)dat.m_LASFile.PointRecord.NIR;
											}
										}
#endif
										//pt.Intensity = dat.m_LASFile.PointRecord.FileMarker;

										StrataPointList[StrataPointCount].Elevation = pt.Elevation;
										StrataPointList[StrataPointCount].Intensity = pt.Intensity;
										StrataPointList[StrataPointCount].ReturnNumber = pt.ReturnNumber;

										StrataPointCount++;
										LastReturn = pt.ReturnNumber;
									}

									// sort value list
									qsort(StrataPointList, (size_t)StrataPointCount, sizeof(STRATAPOINT), compareSP);
								}

								// compute percentile values used for profile area
								if (m_ComputePA) {
									// trap some potential errors...first make sure the max point has height/elevation above 0.0
									// 5/1/2020 also added the max() code to force all point heights to be >= 0...anything with a negative height will be forced to a value of 0.0
									if (StrataPointList[StrataPointCount - 1].Elevation > 0.0) {
										SpecialElevPercentile[0] = max(0.0f, StrataPointList[0].Elevation);

										for (k = 1; k < 100; k++) {
											WholePart = (int)((float)(StrataPointCount - 1) * ((float)k) / 100.0f);
											Fraction = ((float)(StrataPointCount - 1) * ((float)k) / 100.0f) - WholePart;

											if (Fraction == 0.0) {
												SpecialElevPercentile[k] = max(0.0f, StrataPointList[WholePart].Elevation);
											}
											else {
												SpecialElevPercentile[k] = max(0.0f, StrataPointList[WholePart].Elevation) + Fraction * (max(0.0f, StrataPointList[WholePart + 1].Elevation) - max(0.0f, StrataPointList[WholePart].Elevation));
											}
										}

										SpecialElevPercentile[100] = max(0.0f, StrataPointList[StrataPointCount - 1].Elevation);

										// trap some potential errors... P99 must be > 0.0
										if (SpecialElevPercentile[99] > 0.0) {
											// compute profile area using composite trapezoid rule
											// I think it would be fairly easy to set this up so the user could specify the percentile value used to normalize the heights.
											// Basically, the 99 values would be replaced with a variable and the loop from 1 - 99 would run to the variable
											// The only part I'm not sure about is the area for the heights above that used for normalization. It might make sense to just 
											// use the percentiles up to the one used to normalize. the original code uses all normalized percentile heights so the 100th (max value)
											// normalized height is usually slightly greater than 1.0. Not sure if this a problem...probably not since any comparisons would be computed
											// the same way but this does allow the max height to influence the profile area so if there are high outliers, you could get a much higher PA.
											ProfileArea = SpecialElevPercentile[0] / SpecialElevPercentile[99];
											// this code doesn't use the 100th percentile value in the area calculation
											for (k = 1; k < 99; k++)
												ProfileArea += 2.0 * SpecialElevPercentile[k] / SpecialElevPercentile[99];
											ProfileArea += SpecialElevPercentile[99] / SpecialElevPercentile[99];

											// this code uses the 100th percentile value in the area calculation
											//for (k = 1; k < 100; k++)
											//	ProfileArea += 2.0 * SpecialElevPercentile[k] / SpecialElevPercentile[99];
											//ProfileArea += SpecialElevPercentile[100] / SpecialElevPercentile[99];

											ProfileArea *= 0.5;
										}
										else
											ProfileArea = -9999.0;
									}
									else
										ProfileArea = -9999.0;
								}

								// go back through data and compute height strata metrics...first do elevation strata
								if (m_DoHeightStrata) {
									for (l = 0; l < StrataPointCount; l++) {
										// figure out which strata the point is in, accumulate counts and compute mean and variance
										// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
										// modified algorithm to use (n - 1) instead of n (in final calculation outside loop) for kurtosis and skewness to match
										// the values computed in "old" logic (non-strata)
										for (k = 0; k < m_HeightStrataCount; k++) {
											if (StrataPointList[l].Elevation < m_HeightStrata[k]) {
												// ElevStrataCount[] has total return count
												n1 = ElevStrataCount[k];
												ElevStrataCount[k] ++;

												// compute mean, variance, skewness, and kurtosis
												delta = (double)StrataPointList[l].Elevation - ElevStrataMean[k];
												delta_n = delta / (double)ElevStrataCount[k];
												delta_n2 = delta_n * delta_n;

												term1 = delta * delta_n * (double)n1;
												ElevStrataMean[k] += delta_n;
												ElevStrataM4[k] += term1 * delta_n2 * ((double)ElevStrataCount[k] * (double)ElevStrataCount[k] - 3.0 * (double)ElevStrataCount[k] + 3.0) + 6.0 * delta_n2 * ElevStrataM2[k] - 4.0 * delta_n * ElevStrataM3[k];
												ElevStrataM3[k] += term1 * delta_n * ((double)ElevStrataCount[k] - 2.0) - 3.0 * delta_n * ElevStrataM2[k];
												ElevStrataM2[k] += term1;

												// bins 0-8 have counts for returns 1-9
												// bin 9 has count of "other" returns
												if (StrataPointList[l].ReturnNumber < 10 && StrataPointList[l].ReturnNumber > 0) {
													ElevStrataCountReturn[k][StrataPointList[l].ReturnNumber - 1] ++;
												}
												else {
													ElevStrataCountReturn[k][9] ++;
												}

												// do min/max
												ElevStrataMin[k] = min(ElevStrataMin[k], StrataPointList[l].Elevation);
												ElevStrataMax[k] = max(ElevStrataMax[k], StrataPointList[l].Elevation);

												break;
											}
										}
									}

									// compute the final values
									for (k = 0; k < m_HeightStrataCount; k++) {
										if (ElevStrataCount[k]) {
											ElevStrataVariance[k] = ElevStrataM2[k] / (double)(ElevStrataCount[k] - 1);

											// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//											ElevStrataKurtosis[k] = ((double) ElevStrataCount[k] * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]) - 3.0;
											ElevStrataKurtosis[k] = (((double)ElevStrataCount[k] - 1.0) * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]);
											ElevStrataSkewness[k] = (sqrt((double)ElevStrataCount[k] - 1.0) * ElevStrataM3[k]) / sqrt(ElevStrataM2[k] * ElevStrataM2[k] * ElevStrataM2[k]);
										}
									}

									// compute median and mode
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataCount; k++) {
										if (ElevStrataCount[k]) {
											WholePart = (int)((float)(ElevStrataCount[k] - 1) * 0.5f);
											Fraction = ((float)(ElevStrataCount[k] - 1) * 0.5f) - WholePart;

											WholePart = TempPointCount + WholePart;

											if (Fraction == 0.0) {
												ElevStrataMedian[k] = StrataPointList[WholePart].Elevation;
											}
											else {
												ElevStrataMedian[k] = StrataPointList[WholePart].Elevation + Fraction * (StrataPointList[WholePart + 1].Elevation - StrataPointList[WholePart].Elevation);
											}
										}
										else {
											ElevStrataMedian[k] = -9999.0;
										}

										TempPointCount += ElevStrataCount[k];
									}

									// figure out the mode using 64 bins for data...min and max in strata have to be different
									// count values using bins
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataCount; k++) {
										if (ElevStrataCount[k]) {
											if (ElevStrataMin[k] != ElevStrataMax[k]) {
												for (l = 0; l < NUMBEROFBINS; l++)
													Bins[l] = 0;

												for (l = TempPointCount; l < TempPointCount + ElevStrataCount[k]; l++) {
													TheBin = (int)((((double)StrataPointList[l].Elevation - ElevStrataMin[k]) / (ElevStrataMax[k] - ElevStrataMin[k])) * (double)(NUMBEROFBINS - 1));
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

								// do intensity strata
								if (m_DoHeightStrataIntensity) {
									for (l = 0; l < StrataPointCount; l++) {
										// figure out which strata the point is in, accumulate counts and compute mean and variance
										// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
										for (k = 0; k < m_HeightStrataIntensityCount; k++) {
											if (StrataPointList[l].Elevation < m_HeightStrataIntensity[k]) {
												// ElevStrataCount[] has total return count
												n1 = IntStrataCount[k];
												IntStrataCount[k] ++;

												// compute mean, variance, skewness, and kurtosis
												delta = (double)StrataPointList[l].Intensity - IntStrataMean[k];
												delta_n = delta / (double)IntStrataCount[k];
												delta_n2 = delta_n * delta_n;

												term1 = delta * delta_n * (double)n1;
												IntStrataMean[k] += delta_n;
												IntStrataM4[k] += term1 * delta_n2 * ((double)IntStrataCount[k] * (double)IntStrataCount[k] - 3.0 * (double)IntStrataCount[k] + 3.0) + 6.0 * delta_n2 * IntStrataM2[k] - 4.0 * delta_n * IntStrataM3[k];
												IntStrataM3[k] += term1 * delta_n * ((double)IntStrataCount[k] - 2.0) - 3.0 * delta_n * IntStrataM2[k];
												IntStrataM2[k] += term1;

												// bins 0-8 have counts for returns 1-9
												// bin 9 has count of "other" returns
												if (StrataPointList[l].ReturnNumber < 10 && StrataPointList[l].ReturnNumber > 0) {
													IntStrataCountReturn[k][StrataPointList[l].ReturnNumber - 1] ++;
												}
												else {
													IntStrataCountReturn[k][9] ++;
												}

												// do min/max
												IntStrataMin[k] = min(IntStrataMin[k], StrataPointList[l].Intensity);
												IntStrataMax[k] = max(IntStrataMax[k], StrataPointList[l].Intensity);

												break;
											}
										}
									}

									// compute the final values
									for (k = 0; k < m_HeightStrataIntensityCount; k++) {
										if (IntStrataCount[k]) {
											IntStrataVariance[k] = IntStrataM2[k] / (double)(IntStrataCount[k] - 1);

											// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//											IntStrataKurtosis[k] = ((double) IntStrataCount[k] * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]) - 3.0;
											IntStrataKurtosis[k] = (((double)IntStrataCount[k] - 1.0) * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]);
											IntStrataSkewness[k] = (sqrt((double)IntStrataCount[k] - 1.0) * IntStrataM3[k]) / sqrt(IntStrataM2[k] * IntStrataM2[k] * IntStrataM2[k]);
										}
									}

									// resort the list using intensity values within each height strata
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k++) {
										if (IntStrataCount[k]) {
											qsort(&StrataPointList[TempPointCount], (size_t)IntStrataCount[k], sizeof(STRATAPOINT), compareSPint);
										}
										TempPointCount += IntStrataCount[k];
									}

									// compute median and mode
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k++) {
										if (IntStrataCount[k]) {
											WholePart = (int)((float)(IntStrataCount[k] - 1) * 0.5f);
											Fraction = ((float)(IntStrataCount[k] - 1) * 0.5f) - WholePart;

											WholePart = TempPointCount + WholePart;

											if (Fraction == 0.0) {
												IntStrataMedian[k] = StrataPointList[WholePart].Intensity;
											}
											else {
												IntStrataMedian[k] = StrataPointList[WholePart].Intensity + Fraction * (StrataPointList[WholePart + 1].Intensity - StrataPointList[WholePart].Intensity);
											}
										}
										else {
											IntStrataMedian[k] = -9999.0;
										}

										TempPointCount += IntStrataCount[k];
									}

									// figure out the mode using 64 bins for data
									// count values using bins
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k++) {
										if (IntStrataCount[k]) {
											if (IntStrataMin[k] != IntStrataMax[k]) {
												for (l = 0; l < NUMBEROFBINS; l++)
													Bins[l] = 0;

												for (l = TempPointCount; l < TempPointCount + IntStrataCount[k]; l++) {
													TheBin = (int)((((double)StrataPointList[l].Intensity - IntStrataMin[k]) / (IntStrataMax[k] - IntStrataMin[k])) * (double)(NUMBEROFBINS - 1));
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

								if (m_DoHeightStrata || m_DoHeightStrataIntensity || m_ComputePA) {
									delete[] StrataPointList;
								}
							}

							// if we have more than 1 point above height threshold, we can compute means
							if (PointCount >= 1) {
								// compute means
								ElevMean = ElevMean / (double) PointCount;
								IntMean = IntMean / (double) PointCount;
							}

							// must have at least 4 points above height threshold for most metrics
							if (PointCount >= 4) {
								// compute canopy relief ratio...doesn't have any meaning with less than 4 points
								if (ElevMax != ElevMin)
									CanopyReliefRatio = (ElevMean - ElevMin) / (ElevMax - ElevMin);
								else
									CanopyReliefRatio = 0.0;

								// allocate memory for list to get median and Quartile values
								ElevValueList = new float[TotalPointCount];
								IntValueList = new float[TotalPointCount];

								// initialize
								ElevVariance = 0.0;
								ElevKurtosis = 0.0;
								ElevSkewness = 0.0;
								ElevAAD = 0.0;
								ElevSumSquare = 0.0;
								ElevSumCube = 0.0;

								IntVariance = 0.0;
								IntKurtosis = 0.0;
								IntSkewness = 0.0;
								IntAAD = 0.0;

								// reset point count so we can use it as an array index
								PointCount = 0;

								// read all returns and do statistics
								dat.Rewind();
								LastReturn = 9999;
								while (dat.ReadNextRecord(&pt)) {
									if (dat.LastPointIsWithheld())
										continue;

									if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
										continue;
	
									if (m_UseFirstReturns && pt.ReturnNumber > 1) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseFirstReturnInPulse && (pt.ReturnNumber > 1 && pt.ReturnNumber > LastReturn)) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseHeightMin && pt.Elevation <= m_MinHeight) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseHeightMax && pt.Elevation >= m_MaxHeight) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_EliminateOutliers && (pt.Elevation >= m_OutlierMaxHt || pt.Elevation <= m_OutlierMinHt)) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									// if doing metrics with RGB values, swap point intensity for the desired value
#ifdef USE_LASLIB
									if (m_DoRGBMetrics) {
										if (m_RGBColor == RGB_RED)
											pt.Intensity = (float) dat.lasreader->point.rgb[0];
										else if (m_RGBColor == RGB_GREEN)
											pt.Intensity = (float) dat.lasreader->point.rgb[1];
										else if (m_RGBColor == RGB_BLUE)
											pt.Intensity = (float) dat.lasreader->point.rgb[2];
									}
#else
									if (dat.m_HaveLASZIP_DLL) {
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												pt.Intensity = dat.lasdll_point->rgb[0];
											else if (m_RGBColor == RGB_GREEN)
												pt.Intensity = dat.lasdll_point->rgb[1];
											else if (m_RGBColor == RGB_BLUE)
												pt.Intensity = dat.lasdll_point->rgb[2];
											else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
												pt.Intensity = dat.lasdll_point->rgb[3];
										}
									}
									else {
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												pt.Intensity = (float) dat.m_LASFile.PointRecord.Red;
											else if (m_RGBColor == RGB_GREEN)
												pt.Intensity = (float) dat.m_LASFile.PointRecord.Green;
											else if (m_RGBColor == RGB_BLUE)
												pt.Intensity = (float) dat.m_LASFile.PointRecord.Blue;
											else if (m_RGBColor == RGB_NIR && m_DoNIRMetrics)
												pt.Intensity = (float)dat.m_LASFile.PointRecord.NIR;
										}
									}
#endif
//pt.Intensity = dat.m_LASFile.PointRecord.FileMarker;

									ElevVariance += ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean);
									ElevSkewness += ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean);
									ElevKurtosis += ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean) * ((double) pt.Elevation - ElevMean);
									ElevAAD += fabs(((double) pt.Elevation - ElevMean));

									ElevSumSquare += (double) pt.Elevation * (double) pt.Elevation;
									ElevSumCube += (double) pt.Elevation * (double) pt.Elevation * (double) pt.Elevation;

									IntVariance += ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean);
									IntSkewness += ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean);
									IntKurtosis += ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean) * ((double) pt.Intensity - IntMean);
									IntAAD += fabs(((double) pt.Intensity - IntMean));

									// add value to list for median
									ElevValueList[PointCount] = pt.Elevation;
									IntValueList[PointCount] = pt.Intensity;

									PointCount ++;

									LastReturn = pt.ReturnNumber;
								}

								// compute statistics
								ElevVariance = ElevVariance / ((double) (PointCount - 1));
								ElevStdDev = sqrt(ElevVariance);
								ElevSkewness = ElevSkewness / ((double) (PointCount - 1) * ElevStdDev * ElevStdDev * ElevStdDev);
								ElevKurtosis = ElevKurtosis / ((double) (PointCount - 1) * ElevStdDev * ElevStdDev * ElevStdDev * ElevStdDev);
								ElevAAD = ElevAAD / (double) PointCount;

								ElevSumSquare = ElevSumSquare / ((double) (PointCount));
								ElevSumCube = ElevSumCube / ((double) (PointCount));
								ElevSumSquare = sqrt(ElevSumSquare);
								ElevSumCube = pow(ElevSumCube, 0.3333333);

								// cover was calculated here 7/28/2009

								IntVariance = IntVariance / ((double) (PointCount - 1));
								IntStdDev = sqrt(IntVariance);
								IntSkewness = IntSkewness / ((double) (PointCount - 1) * IntStdDev * IntStdDev * IntStdDev);
								IntKurtosis = IntKurtosis / ((double) (PointCount - 1) * IntStdDev * IntStdDev * IntStdDev * IntStdDev);
								IntAAD = IntAAD / (double) PointCount;

								// sort value list
								qsort(ElevValueList, (size_t) PointCount, sizeof(float), compareflt);
								qsort(IntValueList, (size_t) PointCount, sizeof(float), compareflt);

								// compute percentile related metrics
								// source http://www.resacorp.com/quartiles.htm...method 5
								ElevPercentile[0] = (float)ElevMin;
								IntPercentile[0] = (float) IntMin;
								for (k = 1; k < 20; k ++) {
									WholePart = (int) ((float) (PointCount - 1) * ((float) k * 5.0) / 100.0);
									Fraction = ((float) (PointCount - 1) * ((float) k * 5.0f) / 100.0f) - WholePart;
								
									if (Fraction == 0.0) {
										ElevPercentile[k] = ElevValueList[WholePart];
										IntPercentile[k] = IntValueList[WholePart];
									}
									else {
										ElevPercentile[k] = ElevValueList[WholePart] + Fraction * (ElevValueList[WholePart + 1] - ElevValueList[WholePart]);
										IntPercentile[k] = IntValueList[WholePart] + Fraction * (IntValueList[WholePart + 1] - IntValueList[WholePart]);
									}
								}

								ElevPercentile[20] = (float)ElevMax;
								IntPercentile[20] = (float) IntMax;

								// compute P01 and P99 values
								WholePart = (int) ((float) (PointCount - 1) * 1.0f / 100.0f);
								Fraction = ((float) (PointCount - 1) * 1.0f / 100.0f) - WholePart;
							
								if (Fraction == 0.0) {
									ElevP01 = ElevValueList[WholePart];
									IntP01 = IntValueList[WholePart];
								}
								else {
									ElevP01 = ElevValueList[WholePart] + Fraction * (ElevValueList[WholePart + 1] - ElevValueList[WholePart]);
									IntP01 = IntValueList[WholePart] + Fraction * (IntValueList[WholePart + 1] - IntValueList[WholePart]);
								}

								WholePart = (int) ((float) (PointCount - 1) * 99.0f / 100.0f);
								Fraction = ((float) (PointCount - 1) * 99.0f / 100.0f) - WholePart;
							
								if (Fraction == 0.0) {
									ElevP99 = ElevValueList[WholePart];
									IntP99 = IntValueList[WholePart];
								}
								else {
									ElevP99 = ElevValueList[WholePart] + Fraction * (ElevValueList[WholePart + 1] - ElevValueList[WholePart]);
									IntP99 = IntValueList[WholePart] + Fraction * (IntValueList[WholePart + 1] - IntValueList[WholePart]);
								}

								// compute metrics
								ElevMedian = ElevPercentile[10];

								ElevP05 = ElevPercentile[1];
								ElevP10 = ElevPercentile[2];
								ElevP20 = ElevPercentile[4];
								ElevP25 = ElevPercentile[5];
								ElevP30 = ElevPercentile[6];
								ElevP40 = ElevPercentile[8];
								ElevP50 = ElevPercentile[10];
								ElevP60 = ElevPercentile[12];
								ElevP70 = ElevPercentile[14];
								ElevP75 = ElevPercentile[15];
								ElevP80 = ElevPercentile[16];
								ElevP90 = ElevPercentile[18];
								ElevP95 = ElevPercentile[19];

								// compute interquartile distance
								ElevIQDist = ElevP75 - ElevP25;

								IntMedian = IntPercentile[10];

								IntP05 = IntPercentile[1];
								IntP10 = IntPercentile[2];
								IntP20 = IntPercentile[4];
								IntP25 = IntPercentile[5];
								IntP30 = IntPercentile[6];
								IntP40 = IntPercentile[8];
								IntP50 = IntPercentile[10];
								IntP60 = IntPercentile[12];
								IntP70 = IntPercentile[14];
								IntP75 = IntPercentile[15];
								IntP80 = IntPercentile[16];
								IntP90 = IntPercentile[18];
								IntP95 = IntPercentile[19];

								// compute interquartile distance
								IntIQDist = IntP75 - IntP25;

								// check the min and max values for the cell...can only compute the mode if they are different
//								int MaxCount = -1;
								if (ElevMin != ElevMax) {
									// figure out the mode using 64 bins for data
									for (k = 0; k < NUMBEROFBINS; k++)
										Bins[k] = 0;

									// count values using bins
									for (k = 0; k < PointCount; k++) {
										TheBin = (int)((((double)ElevValueList[k] - ElevMin) / (ElevMax - ElevMin)) * (double)(NUMBEROFBINS - 1));
										Bins[TheBin] ++;
									}

									// find most frequent value
									MaxCount = -1;
									for (k = 0; k < NUMBEROFBINS; k++) {
										if (Bins[k] > MaxCount) {
											MaxCount = Bins[k];
											TheBin = k;
										}
									}

									// compute mode by un-scaling the bin number
									ElevMode = ElevMin + ((double)TheBin / (double)(NUMBEROFBINS - 1)) * (ElevMax - ElevMin);
								}
								else {
									ElevMode = ElevMin;
								}

								// check the min and max values for the cell...can only compute the mode if they are different
								if (IntMin != IntMax) {
									// figure out the mode using 64 bins for data
									for (k = 0; k < NUMBEROFBINS; k++)
										Bins[k] = 0;

									// count values using bins
									for (k = 0; k < PointCount; k++) {
										TheBin = (int)((((double)IntValueList[k] - IntMin) / (IntMax - IntMin)) * (double)(NUMBEROFBINS - 1));
										Bins[TheBin] ++;
									}

									// find most frequent value
									MaxCount = -1;
									for (k = 0; k < NUMBEROFBINS; k++) {
										if (Bins[k] > MaxCount) {
											MaxCount = Bins[k];
											TheBin = k;
										}
									}

									// compute mode by un-scaling the bin number
									IntMode = IntMin + ((double)TheBin / (double)(NUMBEROFBINS - 1)) * (IntMax - IntMin);
								}
								else {
									IntMode = IntMin;
								}

								// compute L moments & ratios
								// elevation related
								ElevL1 = ElevL2 = ElevL3 = ElevL4 = 0.0;
								for (k = 0; k < PointCount; k ++) {
									CL1 = (k + 1) - 1;
									CL2 = CL1 * (double) ((k + 1) - 1 - 1) / 2.0;
									CL3 = CL2 * (double) ((k + 1) - 1 - 2) / 3.0;

									CR1 = PointCount - (k + 1);
									CR2 = CR1 * (double) (PointCount - (k + 1) - 1) / 2.0;
									CR3 = CR2 * (double) (PointCount - (k + 1) - 2) / 3.0;

									ElevL1 = ElevL1 + ElevValueList[k];
									ElevL2 = ElevL2 + (CL1 - CR1) * ElevValueList[k];
									ElevL3 = ElevL3 + (CL2 - 2.0 * CL1 * CR1 + CR2) * ElevValueList[k];
									ElevL4 = ElevL4 + (CL3 - 3.0 * CL2 * CR1 + 3.0 * CL1 * CR2 - CR3) * ElevValueList[k];
								}
								C1 = PointCount;
								C2 = C1 * (double) (PointCount - 1) / 2.0;
								C3 = C2 * (double) (PointCount - 2) / 3.0;
								C4 = C3 * (double) (PointCount - 3) / 4.0;
								ElevL1 = ElevL1 / C1;
								ElevL2 = ElevL2 / C2 / 2.0;
								ElevL3 = ElevL3 / C3 / 3.0;
								ElevL4 = ElevL4 / C4 / 4.0;

								// intensity related
								IntL1 = IntL2 = IntL3 = IntL4 = 0.0;
								for (k = 0; k < PointCount; k ++) {
									CL1 = (k + 1) - 1;
									CL2 = CL1 * (double) ((k + 1) - 1 - 1) / 2.0;
									CL3 = CL2 * (double) ((k + 1) - 1 - 2) / 3.0;

									CR1 = PointCount - (k + 1);
									CR2 = CR1 * (double) (PointCount - (k + 1) - 1) / 2.0;
									CR3 = CR2 * (double) (PointCount - (k + 1) - 2) / 3.0;

									IntL1 = IntL1 + IntValueList[k];
									IntL2 = IntL2 + (CL1 - CR1) * IntValueList[k];
									IntL3 = IntL3 + (CL2 - 2.0 * CL1 * CR1 + CR2) * IntValueList[k];
									IntL4 = IntL4 + (CL3 - 3.0 * CL2 * CR1 + 3.0 * CL1 * CR2 - CR3) * IntValueList[k];
								}
								C1 = PointCount;
								C2 = C1 * (double) (PointCount - 1) / 2.0;
								C3 = C2 * (double) (PointCount - 2) / 3.0;
								C4 = C3 * (double) (PointCount - 3) / 4.0;
								IntL1 = IntL1 / C1;
								IntL2 = IntL2 / C2 / 2.0;
								IntL3 = IntL3 / C3 / 3.0;
								IntL4 = IntL4 / C4 / 4.0;

								// if computing cover, do processing
								if (ComputeCover) {
									FirstReturnsAbove = 0;
									FirstReturnsTotal = 0;
									AllReturnsAbove = 0;
									AllReturnsTotal = 0;

									// read all returns and do statistics
									dat.Rewind();
									while (dat.ReadNextRecord(&pt)) {
										if (dat.LastPointIsWithheld())
											continue;

										if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
											continue;

										if (m_UseAllReturnsForCover) {
											if (pt.Elevation > CoverCutoff)
												AllReturnsAbove ++;

											AllReturnsTotal ++;
										}
										
										if (pt.ReturnNumber == 1) {
											if (pt.Elevation > CoverCutoff)
												FirstReturnsAbove ++;

											FirstReturnsTotal ++;
										}
									}
									
									if (m_UseAllReturnsForCover) {
										if (AllReturnsTotal)
											AllCover = (double) AllReturnsAbove / (double) AllReturnsTotal * 100.0;
										else
											AllCover = -1.0;

										if (FirstReturnsTotal)
											AllFirstCover = (double) AllReturnsAbove / (double) FirstReturnsTotal * 100.0;
										else
											AllFirstCover = -1.0;
									}

									if (FirstReturnsTotal)
										Cover = (double) FirstReturnsAbove / (double) FirstReturnsTotal * 100.0;
									else
										Cover = -1.0;
								}
								else
									Cover = -1.0;

								double FirstCoverMean = 0.0;
								double FirstCoverMode = 0.0;
								double AllCoverMean = 0.0;
								double AllCoverMode = 0.0;
								double AllFirstCoverMean = 0.0;
								double AllFirstCoverMode = 0.0;
								int AllAboveMean = 0;
								int AllAboveMode = 0;
								int FirstAboveMean = 0;
								int FirstAboveMode = 0;
								if (m_ComputeRelCover) {
									// compute cover relative to the mean and mode
									FirstReturnsTotal = 0;
									AllReturnsTotal = 0;

									// read all returns and do statistics
									dat.Rewind();
									while (dat.ReadNextRecord(&pt)) {
										if (dat.LastPointIsWithheld())
											continue;

										if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
											continue;

										if (m_UseAllReturnsForCover) {
											if (pt.Elevation > ElevMean)
												AllAboveMean ++;
											if (pt.Elevation > ElevMode)
												AllAboveMode ++;

											AllReturnsTotal ++;
										}
										
										if (pt.ReturnNumber == 1) {
											if (pt.Elevation > ElevMean)
												FirstAboveMean ++;
											if (pt.Elevation > ElevMode)
												FirstAboveMode ++;

											FirstReturnsTotal ++;
										}
									}
									
									if (AllReturnsTotal) {
										AllCoverMean = (double) AllAboveMean / (double) AllReturnsTotal * 100.0;
										AllCoverMode = (double) AllAboveMode / (double) AllReturnsTotal * 100.0;
									}
									else {
										AllCoverMean = -1.0;
										AllCoverMode = -1.0;
									}

									if (FirstReturnsTotal) {
										AllFirstCoverMean = (double) AllAboveMean / (double) FirstReturnsTotal * 100.0;
										AllFirstCoverMode = (double) AllAboveMode / (double) FirstReturnsTotal * 100.0;
										FirstCoverMean = (double) FirstAboveMean / (double) FirstReturnsTotal * 100.0;
										FirstCoverMode = (double) FirstAboveMode / (double) FirstReturnsTotal * 100.0;
									}
									else {
										AllFirstCover = -1.0;
										AllFirstCover = -1.0;
										FirstCoverMean = -1.0;
										FirstCoverMode = -1.0;
									}
								}

// ****************************
								// compute kernal density stuff
								if (m_DoKDEStats) {
									// should be able to use ElevValueList...it has sorted elevation values
									double BW;
									BW = 0.9 * (min(ElevStdDev, ElevIQDist) / 1.34) * pow((double) PointCount, -0.2);
									GaussianKDE(ElevValueList, PointCount, BW * m_KDEBandwidthMultiplier, m_KDEWindowSize, KDE_ModeCount, KDE_MinMode, KDE_MaxMode);
									KDE_ModeRange = KDE_MaxMode - KDE_MinMode;
								}

								// go through data and compute median absolute deviations from median and mode
								// use ElevValueList and IntValueList to accumulate values
								TempPointCount = 0;
								dat.Rewind();
								while (dat.ReadNextRecord(&pt)) { 
									if (dat.LastPointIsWithheld())
										continue;

									if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
										continue;

									if (m_UseHeightMin && pt.Elevation <= m_MinHeight) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseHeightMax && pt.Elevation >= m_MaxHeight) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_EliminateOutliers && (pt.Elevation >= m_OutlierMaxHt || pt.Elevation <= m_OutlierMinHt)) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseFirstReturns && pt.ReturnNumber > 1) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									if (m_UseFirstReturnInPulse && (pt.ReturnNumber > 1 && pt.ReturnNumber > LastReturn)) {
										LastReturn = pt.ReturnNumber;
										continue;
									}

									ElevValueList[TempPointCount] = (float) fabs((double) pt.Elevation - ElevMedian);
									IntValueList[TempPointCount] = (float) fabs((double) pt.Elevation - ElevMode);

									TempPointCount ++;
								}

								// sort value list
								qsort(ElevValueList, (size_t) TempPointCount, sizeof(float), compareflt);
								qsort(IntValueList, (size_t) TempPointCount, sizeof(float), compareflt);

								// compute median values
								// source http://www.resacorp.com/quartiles.htm...method 5
								WholePart = (int) ((float) (TempPointCount - 1) * 0.5f);
								Fraction = ((float) (TempPointCount - 1) * 0.5f) - WholePart;
							
								if (Fraction == 0.0) {
									ElevMadMedian = ElevValueList[WholePart];
									ElevMadMode = IntValueList[WholePart];
								}
								else {
									ElevMadMedian = ElevValueList[WholePart] + Fraction * (ElevValueList[WholePart + 1] - ElevValueList[WholePart]);
									ElevMadMode = IntValueList[WholePart] + Fraction * (IntValueList[WholePart + 1] - IntValueList[WholePart]);
								}

/*								// allocate space for a list of point elevations and intensity values
								if (m_DoHeightStrata || m_DoHeightStrataIntensity || m_OutputPercentileData) {
									StrataPointList = (STRATAPOINT*) new STRATAPOINT[TotalPointCount];

									// fill list with points
									dat.Rewind();
									LastReturn = 9999;
									StrataPointCount = 0;
									while (dat.ReadNextRecord(&pt)) { 
										if (dat.LastPointIsWithheld())
											continue;

										if (dat.LastPointIsOverlap() && m_SkipOverlapPoints)
											continue;

										if (m_UseFirstReturns && pt.ReturnNumber > 1) {
											LastReturn = pt.ReturnNumber;
											continue;
										}

										if (m_UseFirstReturnInPulse && (pt.ReturnNumber > 1 && pt.ReturnNumber > LastReturn)) {
											LastReturn = pt.ReturnNumber;
											continue;
										}

										// if doing metrics with RGB values, swap point intensity for the desired value
#ifdef USE_LASLIB
										if (m_DoRGBMetrics) {
											if (m_RGBColor == RGB_RED)
												pt.Intensity = (float) dat.lasreader->point.rgb[0];
											else if (m_RGBColor == RGB_GREEN)
												pt.Intensity = (float) dat.lasreader->point.rgb[1];
											else if (m_RGBColor == RGB_BLUE)
												pt.Intensity = (float) dat.lasreader->point.rgb[2];
										}
#else
										if (dat.m_HaveLASZIP_DLL) {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													pt.Intensity = dat.lasdll_point->rgb[0];
												else if (m_RGBColor == RGB_GREEN)
													pt.Intensity = dat.lasdll_point->rgb[1];
												else if (m_RGBColor == RGB_BLUE)
													pt.Intensity = dat.lasdll_point->rgb[2];
											}
										}
										else {
											if (m_DoRGBMetrics) {
												if (m_RGBColor == RGB_RED)
													pt.Intensity = (float) dat.m_LASFile.PointRecord.Red;
												else if (m_RGBColor == RGB_GREEN)
													pt.Intensity = (float) dat.m_LASFile.PointRecord.Green;
												else if (m_RGBColor == RGB_BLUE)
													pt.Intensity = (float) dat.m_LASFile.PointRecord.Blue;
											}
										}
#endif
//pt.Intensity = dat.m_LASFile.PointRecord.FileMarker;

										StrataPointList[StrataPointCount].Elevation = pt.Elevation;
										StrataPointList[StrataPointCount].Intensity = pt.Intensity;
										StrataPointList[StrataPointCount].ReturnNumber = pt.ReturnNumber;

										StrataPointCount ++;
										LastReturn = pt.ReturnNumber;
									}

									// sort value list
									qsort(StrataPointList, (size_t) StrataPointCount, sizeof(STRATAPOINT), compareSP);
								}

								// compute percentile values used for profile area
								// **********************************************************************************************
								// **********************************************************************************************
								// **********************************************************************************************
								// **********************************************************************************************
								// 5/1/2020 NOTE: this code does not have the logic to check values before computing profile area
								if (m_OutputPercentileData) {
									SpecialElevPercentile[0] = StrataPointList[0].Elevation;
									
									for (k = 1; k < 100; k++) {
										WholePart = (int)((float)(StrataPointCount - 1) * ((float)k) / 100.0f);
										Fraction = ((float)(StrataPointCount - 1) * ((float)k) / 100.0f) - WholePart;

										if (Fraction == 0.0) {
											SpecialElevPercentile[k] = StrataPointList[WholePart].Elevation;
										}
										else {
											SpecialElevPercentile[k] = StrataPointList[WholePart].Elevation + Fraction * (StrataPointList[WholePart + 1].Elevation - StrataPointList[WholePart].Elevation);
										}
									}

									SpecialElevPercentile[100] = StrataPointList[StrataPointCount - 1].Elevation;

									// compute profile area using composite trapezoid rule
									ProfileArea = SpecialElevPercentile[0] / SpecialElevPercentile[99];
									for (k = 1; k < 100; k++)
										ProfileArea += 2.0 * SpecialElevPercentile[k] / SpecialElevPercentile[99];
									ProfileArea += SpecialElevPercentile[100] / SpecialElevPercentile[99];
									ProfileArea *= 0.5;
								}

								// go back through data and compute height strata metrics...first do elevation strata
								if (m_DoHeightStrata) {
									for (l = 0; l < StrataPointCount; l ++) {
										// figure out which strata the point is in, accumulate counts and compute mean and variance
										// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
										// modified algorithm to use (n - 1) instead of n (in final calculation outside loop) for kurtosis and skewness to match
										// the values computed in "old" logic (non-strata)
										for (k = 0; k < m_HeightStrataCount; k ++) {
											if (StrataPointList[l].Elevation < m_HeightStrata[k]) {
												// ElevStrataCount[] has total return count
												n1 = ElevStrataCount[k];
												ElevStrataCount[k] ++;

												// compute mean, variance, skewness, and kurtosis
												delta = (double) StrataPointList[l].Elevation - ElevStrataMean[k];
												delta_n = delta / (double) ElevStrataCount[k];
												delta_n2 = delta_n * delta_n;

												term1 = delta * delta_n * (double) n1;
												ElevStrataMean[k] += delta_n;
												ElevStrataM4[k] += term1 * delta_n2 * ((double) ElevStrataCount[k] * (double) ElevStrataCount[k] - 3.0 * (double) ElevStrataCount[k] + 3.0) + 6.0 * delta_n2 * ElevStrataM2[k] - 4.0 * delta_n * ElevStrataM3[k];
												ElevStrataM3[k] += term1 * delta_n * ((double) ElevStrataCount[k] - 2.0) - 3.0 * delta_n * ElevStrataM2[k];
												ElevStrataM2[k] += term1;

												// bins 0-8 have counts for returns 1-9
												// bin 9 has count of "other" returns
												if (StrataPointList[l].ReturnNumber < 10 && StrataPointList[l].ReturnNumber > 0) {
													ElevStrataCountReturn[k][StrataPointList[l].ReturnNumber - 1] ++;
												}
												else {
													ElevStrataCountReturn[k][9] ++;
												}

												// do min/max
												ElevStrataMin[k] = min(ElevStrataMin[k], StrataPointList[l].Elevation);
												ElevStrataMax[k] = max(ElevStrataMax[k], StrataPointList[l].Elevation);

												break;
											}
										}
									}

									// compute the final values
									for (k = 0; k < m_HeightStrataCount; k ++) {
										if (ElevStrataCount[k]) {
											ElevStrataVariance[k] = ElevStrataM2[k] / (double) (ElevStrataCount[k] - 1);

											// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//											ElevStrataKurtosis[k] = ((double) ElevStrataCount[k] * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]) - 3.0;
											ElevStrataKurtosis[k] = (((double) ElevStrataCount[k] - 1.0) * ElevStrataM4[k]) / (ElevStrataM2[k] * ElevStrataM2[k]);
											ElevStrataSkewness[k] = (sqrt((double) ElevStrataCount[k] - 1.0) * ElevStrataM3[k]) / sqrt(ElevStrataM2[k] * ElevStrataM2[k] * ElevStrataM2[k]);
										}
									}

									// compute median and mode
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataCount; k ++) {
										if (ElevStrataCount[k]) {
											WholePart = (int) ((float) (ElevStrataCount[k] - 1) * 0.5f);
											Fraction = ((float) (ElevStrataCount[k] - 1) * 0.5f) - WholePart;
										
											WholePart = TempPointCount + WholePart;

											if (Fraction == 0.0) {
												ElevStrataMedian[k] = StrataPointList[WholePart].Elevation;
											}
											else {
												ElevStrataMedian[k] = StrataPointList[WholePart].Elevation + Fraction * (StrataPointList[WholePart + 1].Elevation - StrataPointList[WholePart].Elevation);
											}
										}
										else {
											ElevStrataMedian[k] = -9999.0;
										}

										TempPointCount += ElevStrataCount[k];
									}

									// figure out the mode using 64 bins for data
									// count values using bins
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataCount; k ++) {
										if (ElevStrataCount[k]) {
											for (l = 0; l < NUMBEROFBINS; l ++)
												Bins[l] = 0;

											for (l = TempPointCount; l < TempPointCount + ElevStrataCount[k]; l ++) {
												TheBin = (int) ((((double) StrataPointList[l].Elevation - ElevStrataMin[k]) / (ElevStrataMax[k] - ElevStrataMin[k])) * (double) (NUMBEROFBINS - 1));
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

								// do intensity strata
								if (m_DoHeightStrataIntensity) {
									for (l = 0; l < StrataPointCount; l ++) {
										// figure out which strata the point is in, accumulate counts and compute mean and variance
										// algorithm for 1-pass calculation of mean and std dev from: http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
										for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
											if (StrataPointList[l].Elevation < m_HeightStrataIntensity[k]) {
												// ElevStrataCount[] has total return count
												n1 = IntStrataCount[k];
												IntStrataCount[k] ++;

												// compute mean, variance, skewness, and kurtosis
												delta = (double) StrataPointList[l].Intensity - IntStrataMean[k];
												delta_n = delta / (double) IntStrataCount[k];
												delta_n2 = delta_n * delta_n;

												term1 = delta * delta_n * (double) n1;
												IntStrataMean[k] += delta_n;
												IntStrataM4[k] += term1 * delta_n2 * ((double) IntStrataCount[k] * (double) IntStrataCount[k] - 3.0 * (double) IntStrataCount[k] + 3.0) + 6.0 * delta_n2 * IntStrataM2[k] - 4.0 * delta_n * IntStrataM3[k];
												IntStrataM3[k] += term1 * delta_n * ((double) IntStrataCount[k] - 2.0) - 3.0 * delta_n * IntStrataM2[k];
												IntStrataM2[k] += term1;

												// bins 0-8 have counts for returns 1-9
												// bin 9 has count of "other" returns
												if (StrataPointList[l].ReturnNumber < 10 && StrataPointList[l].ReturnNumber > 0) {
													IntStrataCountReturn[k][StrataPointList[l].ReturnNumber - 1] ++;
												}
												else {
													IntStrataCountReturn[k][9] ++;
												}

												// do min/max
												IntStrataMin[k] = min(IntStrataMin[k], StrataPointList[l].Intensity);
												IntStrataMax[k] = max(IntStrataMax[k], StrataPointList[l].Intensity);

												break;
											}
										}
									}

									// compute the final values
									for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
										if (IntStrataCount[k]) {
											IntStrataVariance[k] = IntStrataM2[k] / (double) (IntStrataCount[k] - 1);

											// kurtosis and skewness use (n - 1) to match values computed for entire point cloud
//											IntStrataKurtosis[k] = ((double) IntStrataCount[k] * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]) - 3.0;
											IntStrataKurtosis[k] = (((double) IntStrataCount[k] - 1.0) * IntStrataM4[k]) / (IntStrataM2[k] * IntStrataM2[k]);
											IntStrataSkewness[k] = (sqrt((double) IntStrataCount[k] - 1.0) * IntStrataM3[k]) / sqrt(IntStrataM2[k] * IntStrataM2[k] * IntStrataM2[k]);
										}
									}

									// resort the list using intensity values within each height strata
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
										if (IntStrataCount[k]) {
											qsort(&StrataPointList[TempPointCount], (size_t) IntStrataCount[k], sizeof(STRATAPOINT), compareSPint);
										}
										TempPointCount += IntStrataCount[k];
									}

									// compute median and mode
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
										if (IntStrataCount[k]) {
											WholePart = (int) ((float) (IntStrataCount[k] - 1) * 0.5f);
											Fraction = ((float) (IntStrataCount[k] - 1) * 0.5f) - WholePart;
										
											WholePart = TempPointCount + WholePart;

											if (Fraction == 0.0) {
												IntStrataMedian[k] = StrataPointList[WholePart].Intensity;
											}
											else {
												IntStrataMedian[k] = StrataPointList[WholePart].Intensity + Fraction * (StrataPointList[WholePart + 1].Intensity - StrataPointList[WholePart].Intensity);
											}
										}
										else {
											IntStrataMedian[k] = -9999.0;
										}

										TempPointCount += IntStrataCount[k];
									}

									// figure out the mode using 64 bins for data
									// count values using bins
									TempPointCount = 0;
									for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
										if (IntStrataCount[k]) {
											for (l = 0; l < NUMBEROFBINS; l ++)
												Bins[l] = 0;

											for (l = TempPointCount; l < TempPointCount + IntStrataCount[k]; l ++) {
												TheBin = (int) ((((double) StrataPointList[l].Intensity - IntStrataMin[k]) / (IntStrataMax[k] - IntStrataMin[k])) * (double) (NUMBEROFBINS - 1));
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

								if (m_DoHeightStrata || m_DoHeightStrataIntensity || m_OutputPercentileData) {
									delete [] StrataPointList;
								}
*/
								// report stuff to csv file
								if (ParseID || ReverseParseID) {
									fprintf(f, "%i,", ID);

									if (m_OutputPercentileData)
										fprintf(pf, "%i,", ID);
								}

								if (m_ProduceHighpointOutput) {
									fprintf(f, "\"%s\",%i,%lf,%lf,%lf\n", (LPCTSTR) ce.m_FileName, PointCount, HighX, HighY, HighElevation);
								}
								else if (m_ProduceYZLiOutput) {
									fprintf(f, "\"%s\",%i,%lf,%lf,%lf,%lf\n", (LPCTSTR) ce.m_FileName, PointCount, ElevMean, ElevStdDev, ElevP75, Cover);
								}
								else {
									// print base values
									if (!m_UseHeightMin && !m_UseHeightMax)
										fprintf(f, "\"%s\",%s,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), TotalPointCount);
// 1/18/2013 changed										fprintf(f, "\"%s\",%s,%i", ce.m_FileName, TempfsForFileTitle.FileTitle(), PointCount);
									else
										fprintf(f, "\"%s\",%s,%i,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), TotalPointCount, PointCount);

									if (m_OutputPercentileData) {
										fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, ElevMax);
										for (k = 0; k < 101; k++) {
											fprintf(pf, ",%lf", SpecialElevPercentile[k]);
										}
										fprintf(pf, ",%lf\n", ProfileArea);
									}

									if (m_CountReturns) {
										for (k = 0; k < 10; k ++)
											fprintf(f, ",%i", ReturnCounts[k]);
									}

//                                                                                   1                                       2                                       3                                       4                                       5                                       6                                       7                                       8
//                                               1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   
									fprintf(f, ",%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", 
											ElevMin, ElevMax, ElevMean, ElevMode, ElevStdDev, ElevVariance, ElevStdDev / ElevMean, ElevIQDist, ElevSkewness, ElevKurtosis, ElevAAD, ElevMadMedian, ElevMadMode,
											ElevL1, ElevL2, ElevL3, ElevL4, ElevL2 / ElevL1, ElevL3 / ElevL2, ElevL4 / ElevL2,
											ElevP01, ElevP05, ElevP10, ElevP20, ElevP25, ElevP30, ElevP40, ElevP50, ElevP60, ElevP70, ElevP75, ElevP80, ElevP90, ElevP95, ElevP99, CanopyReliefRatio, ElevSumSquare, ElevSumCube,
// 2/29/2012 prior to general means											ElevP01, ElevP05, ElevP10, ElevP20, ElevP25, ElevP30, ElevP40, ElevP50, ElevP60, ElevP70, ElevP75, ElevP80, ElevP90, ElevP95, ElevP99, CanopyReliefRatio,
											IntMin, IntMax, IntMean, IntMode, IntStdDev, IntVariance, IntStdDev / IntMean, IntIQDist, IntSkewness, IntKurtosis, IntAAD, 
											IntL1, IntL2, IntL3, IntL4, IntL2 / IntL1, IntL3 / IntL2, IntL4 / IntL2,
											IntP01, IntP05, IntP10, IntP20, IntP25, IntP30, IntP40, IntP50, IntP60, IntP70, IntP75, IntP80, IntP90, IntP95, IntP99);

									if (ComputeCover) {
										fprintf(f, ",%lf", Cover);
										
										if (m_UseAllReturnsForCover) {
											fprintf(f, ",%lf,%lf", AllCover, AllFirstCover);
											fprintf(f, ",%i,%i", FirstReturnsAbove, AllReturnsAbove);
										}
									}
									
									if (m_ComputeRelCover) {
										fprintf(f, ",%lf,%lf,%lf,%lf,%lf,%lf", FirstCoverMean, FirstCoverMode, AllCoverMean, AllCoverMode, AllFirstCoverMean, AllFirstCoverMode);
										fprintf(f, ",%i,%i,%i,%i,%i,%i", FirstAboveMean, FirstAboveMode, AllAboveMean, AllAboveMode, FirstReturnsTotal, AllReturnsTotal);
									}

									if (m_DoKDEStats) {
										// KDE stuff
										fprintf(f, ",%i,%lf,%lf,%lf", KDE_ModeCount, KDE_MinMode, KDE_MaxMode, KDE_ModeRange);
									}

									if (m_DoHeightStrata) {
										for (k = 0; k < m_HeightStrataCount; k ++) {
											if (ElevStrataCount[k] > 2)
												fprintf(f, ",%i,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", ElevStrataCount[k], (double) ElevStrataCount[k] / (double) StrataPointCount, ElevStrataMin[k], ElevStrataMax[k], ElevStrataMean[k], ElevStrataMode[k], ElevStrataMedian[k], sqrt(ElevStrataVariance[k]), sqrt(ElevStrataVariance[k]) / ElevStrataMean[k], ElevStrataSkewness[k], ElevStrataKurtosis[k]);
											else if (ElevStrataCount[k])
												fprintf(f, ",%i,%lf,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", ElevStrataCount[k], (double) ElevStrataCount[k] / (double) StrataPointCount, ElevStrataMean[k]);
											else
												fprintf(f, ",%i,0.0,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", ElevStrataCount[k], ElevStrataMean[k]);
										}
									}

									if (m_DoHeightStrataIntensity) {
										for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
											if (IntStrataCount[k] > 2)
												fprintf(f, ",%i,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", IntStrataCount[k], (double) IntStrataCount[k] / (double) StrataPointCount, IntStrataMin[k], IntStrataMax[k], IntStrataMean[k], IntStrataMode[k], IntStrataMedian[k], sqrt(IntStrataVariance[k]), sqrt(IntStrataVariance[k]) / IntStrataMean[k], IntStrataSkewness[k], IntStrataKurtosis[k]);
											else if (IntStrataCount[k])
												fprintf(f, ",%i,%lf,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", IntStrataCount[k], (double) IntStrataCount[k] / (double) StrataPointCount, IntStrataMean[k]);
											else
												fprintf(f, ",%i,0.0,-9999.0,-9999.0,%lf,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0,-9999.0", IntStrataCount[k], IntStrataMean[k]);
										}
									}

									// print profile area and end of line
									fprintf(f, ",%lf\n", ProfileArea);
								}

								// clean up
								delete [] ElevValueList;
								delete [] IntValueList;

								// status info
								csTemp.Format("   %s: %i points for metrics; %i total points", (LPCSTR)ce.m_FileName, PointCount, TotalPointCount);
								LTKCL_PrintStatus(csTemp);
							}
							else if (PointCount >= 1) {
								// only report min, max, mean
								
								// report stuff to csv file
								if (ParseID || ReverseParseID) {
									fprintf(f, "%i,", ID);

									if (m_OutputPercentileData)
										fprintf(pf, "%i,", ID);
								}

								if (m_ProduceHighpointOutput) {
									fprintf(f, "\"%s\",%i,%lf,%lf,%lf\n", (LPCTSTR) ce.m_FileName, PointCount, HighX, HighY, HighElevation);
								}
								else if (m_ProduceYZLiOutput) {
									fprintf(f, "\"%s\",%i,%lf,%lf,%lf,%lf\n", (LPCTSTR) ce.m_FileName, PointCount, ElevMean, ElevStdDev, ElevP75, Cover);
								}
								else {
									// print base values
									if (!m_UseHeightMin && !m_UseHeightMax)
										fprintf(f, "\"%s\",%s,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), PointCount);
									else
										fprintf(f, "\"%s\",%s,%i,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), TotalPointCount, PointCount);
	//								fprintf(f, "\"%s\",%i", ce.m_FileName, PointCount);

									// report percentile values related to profile area
									if (m_OutputPercentileData) {
										if (TotalPointCount > 0) {
											fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, ElevMax);
											for (k = 0; k < 101; k++) {
												fprintf(pf, ",%lf", SpecialElevPercentile[k]);
											}
											fprintf(pf, ",%lf\n", ProfileArea);
										}
										else {
											fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, ElevMax);
											for (k = 0; k < 101; k++) {
												fprintf(pf, ",%lf", 0.0);
											}
											fprintf(pf, ",0.0\n");
										}
									}

									if (m_CountReturns) {
										for (k = 0; k < 10; k ++)
											fprintf(f, ",%i", ReturnCounts[k]);
									}
//                                                                                   1                                       2                                       3                                       4                                       5                                       6                                       7                                       8
//                                               1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   
									fprintf(f, ",%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", 
											ElevMin, ElevMax, ElevMean, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,			// 15
											0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,																// 7
											0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, CanopyReliefRatio, 0.0, 0.0,	// 16
//											0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, CanopyReliefRatio,
											IntMin, IntMax, IntMean, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,						// 13
											0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,																// 7
											0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);								// 13

									if (ComputeCover) {
										fprintf(f, ",%lf", 0.0);

										if (m_UseAllReturnsForCover) {
											fprintf(f, ",0.0,0.0");
											fprintf(f, ",0,0");
										}
									}

									if (m_ComputeRelCover) {
										fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0");
										fprintf(f, ",0,0,0,0,0,0");
									}

									if (m_DoKDEStats) {
										// KDE stuff
										fprintf(f, ",0,0.0,0.0,0.0");
									}

									if (m_DoHeightStrata) {
										for (k = 0; k < m_HeightStrataCount; k ++) {
											fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
										}
									}

									if (m_DoHeightStrataIntensity) {
										for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
											fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
										}
									}

									// print profile area and end of line
									if (m_ComputePA) {
										if (TotalPointCount > 0) {
											fprintf(f, ",%lf\n", ProfileArea);
										}
										else {
											fprintf(f, ",0.0\n");
										}
									}
								}

								// status info
								csTemp.Format("   %s: %i points for metrics; %i total points...limited metrics computed", (LPCSTR) ce.m_FileName, PointCount, TotalPointCount);
								LTKCL_PrintStatus(csTemp);
							}
							else {
								// not enough points...print zeros to output file
								if (ParseID || ReverseParseID) {
									fprintf(f, "%i,", ID);

									if (m_OutputPercentileData)
										fprintf(pf, "%i,", ID);
								}

								if (m_ProduceHighpointOutput) {
									fprintf(f, "\"%s\",%i,0.0,0.0,0.0\n", (LPCTSTR) ce.m_FileName, PointCount);
								}
								else if (m_ProduceYZLiOutput) {
									fprintf(f, "\"%s\",%i,0.0,0.0,0.0,0.0\n", (LPCTSTR) ce.m_FileName, PointCount);
								}
								else {
									if (!m_UseHeightMin && !m_UseHeightMax)
										fprintf(f, "\"%s\",%s,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), PointCount);
									else
										fprintf(f, "\"%s\",%s,%i,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), TotalPointCount, PointCount);
	//								fprintf(f, "\"%s\",%i", ce.m_FileName, PointCount);

									// report percentile values related to profile area
									if (m_OutputPercentileData) {
										if (TotalPointCount > 0) {
											fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, ElevMax);
											for (k = 0; k < 101; k++) {
												fprintf(pf, ",%lf", SpecialElevPercentile[k]);
											}
											fprintf(pf, ",%lf\n", ProfileArea);
										}
										else {
											fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, ElevMax);
											for (k = 0; k < 101; k++) {
												fprintf(pf, ",%lf", 0.0);
											}
											fprintf(pf, ",0.0\n");
										}
									}

									if (m_CountReturns) {
										for (k = 0; k < 10; k ++)
											fprintf(f, ",%i", ReturnCounts[k]);
									}
//                                                                                   1                                       2                                       3                                       4                                       5                                       6                                       7                                       8
//                                               1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0
									fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
//									fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");

									if (ComputeCover) {
										fprintf(f, ",0.0");

										if (m_UseAllReturnsForCover) {
											fprintf(f, ",0.0,0.0");
											fprintf(f, ",0,0");
										}
									}

									if (m_ComputeRelCover) {
										fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0");
										fprintf(f, ",0,0,0,0,0,0");
									}

									if (m_DoKDEStats) {
										// KDE stuff
										fprintf(f, ",0,0.0,0.0,0.0");
									}

									if (m_DoHeightStrata) {
										for (k = 0; k < m_HeightStrataCount; k ++) {
											fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
										}
									}

									if (m_DoHeightStrataIntensity) {
										for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
											fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
										}
									}

									// print profile area and end of line
									if (m_ComputePA) {
										if (TotalPointCount > 0) {
											fprintf(f, ",%lf\n", ProfileArea);
										}
										else {
											fprintf(f, ",0.0\n");
										}
									}
								}

								// status info
								csTemp.Format("   %s: %i points for metrics; %i total points...limited metrics computed", (LPCSTR) ce.m_FileName, PointCount, TotalPointCount);
//								csTemp.Format("***ERROR: NOT all metrics computed for %s...invalid data file or too few points above height threshold", ce.m_FileName);
								LTKCL_PrintStatus(csTemp);
							}
						}
						else {
							// bad file...print zeros to output file
							if (ParseID || ReverseParseID) {
								fprintf(f, "%i,", ID);

								if (m_OutputPercentileData)
									fprintf(pf, "%i,", ID);
							}

							if (m_ProduceHighpointOutput) {
								fprintf(f, "\"%s\",%i,0.0,0.0,0.0\n", (LPCTSTR) ce.m_FileName, PointCount);
							}
							else if (m_ProduceYZLiOutput) {
								fprintf(f, "\"%s\",%i,0.0,0.0,0.0,0.0\n", (LPCTSTR) ce.m_FileName, PointCount);
							}
							else {
								if (!m_UseHeightMin && !m_UseHeightMax)
									fprintf(f, "\"%s\",%s,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), PointCount);
								else
									fprintf(f, "\"%s\",%s,%i,%i", (LPCTSTR) ce.m_FileName, (LPCTSTR) TempfsForFileTitle.FileTitle(), TotalPointCount, PointCount);
	//							fprintf(f, "\"%s\",%i", ce.m_FileName, PointCount);

								if (m_OutputPercentileData) {
									fprintf(pf, "\"%s\",%lf", (LPCTSTR)ce.m_FileName, 0.0);
									for (k = 0; k < 101; k++) {
										fprintf(pf, ",%lf", 0.0);
									}
									fprintf(pf, ",0.0\n");
								}

								if (m_CountReturns) {
									for (k = 0; k < 10; k ++)
										fprintf(f, ",%i", ReturnCounts[k]);
								}

								//                                                1                                       2                                       3                                       4                                       5
								//            1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2
								fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
//								fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");

								if (ComputeCover) {
									fprintf(f, ",0.0");

									if (m_UseAllReturnsForCover) {
										fprintf(f, ",0.0,0.0");
										fprintf(f, ",0,0");
									}
								}

								if (m_ComputeRelCover) {
									fprintf(f, ",0.0,0.0,0.0,0.0,0.0,0.0");
									fprintf(f, ",0,0,0,0,0,0");
								}

								if (m_DoKDEStats) {
									// KDE stuff
									fprintf(f, ",0,0.0,0.0,0.0");
								}

								if (m_DoHeightStrata) {
									for (k = 0; k < m_HeightStrataCount; k ++) {
										fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
									}
								}

								if (m_DoHeightStrataIntensity) {
									for (k = 0; k < m_HeightStrataIntensityCount; k ++) {
										fprintf(f, ",0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0");
									}
								}

								// print profile area and end of line
								fprintf(f, ",0.0\n");
							}

							// status info
							csTemp.Format("***ERROR: Metrics NOT computed for %s...not a valid data file", (LPCSTR) ce.m_FileName);
							LTKCL_PrintStatus(csTemp);
						}
					}
					fclose(f);

					if (m_OutputPercentileData) {
						fclose(pf);
						LTKCL_ReportProductFile(PercentileOutputFileCL, "CloudMetrics percentile output");
					}

					LTKCL_ReportProductFile(OutputFileCL, "CloudMetrics output");
				}
				else {
					// could not open file...give error
					csTemp.Format("***ERROR: Could not open output file: %s", (LPCSTR) OutputFileCL);
					LTKCL_PrintStatus(csTemp);

					if (m_OutputPercentileData) {
						fclose(pf);
						DeleteFile(PercentileOutputFileCL);
					}

				}
				delete [] ElevStrataCount;
				delete[] ElevStrataMean;
				delete[] ElevStrataMin;
				delete[] ElevStrataMax;
				delete[] ElevStrataMedian;
				delete[] ElevStrataMode;
				delete[] ElevStrataSkewness;
				delete[] ElevStrataKurtosis;
				delete[] ElevStrataVariance;
				delete[] ElevStrataM2;
				delete[] ElevStrataM3;
				delete[] ElevStrataM4;

				delete[] IntStrataCount;
				delete[] IntStrataMean;
				delete[] IntStrataMin;
				delete[] IntStrataMax;
				delete[] IntStrataMedian;
				delete[] IntStrataMode;
				delete[] IntStrataSkewness;
				delete[] IntStrataKurtosis;
				delete[] IntStrataVariance;
				delete[] IntStrataM2;
				delete[] IntStrataM3;
				delete[] IntStrataM4;
			}
			LTKCL_PrintEndReport(m_nRetCode);
		}
		// unload LAZ/LAS dll
		laszip_unload_dll();
	}

	return m_nRetCode;
}

#include "command_line_core_functions.cpp"

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
	for (i = 0; i < Pts; i ++) {
		MinElev = min(MinElev, (double) PointData[i]);
		MaxElev = max(MaxElev, (double) PointData[i]);
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
	double Step = (MaxElev - MinElev) / (double) (STEPS - 1);
	double Constant1 = 1.0 / ((double) Pts * sqrt(2.0 * 3.141592653589793 * BW * BW));
	double Constant2 = 2.0 * BW * BW;
	double ExpTerm;
	double AveY;		// used for sliding window

	// compute probabilites
	double Ht = MinElev;
	for (i = 0; i < STEPS; i ++) {
		ExpTerm = 0.0;
		for (j = 0; j < Pts; j ++) {
			ExpTerm += exp(-1.0 * (Ht - (double) PointData[j]) * (Ht - (double) PointData[j]) / Constant2);
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
		int HalfWindow = (int) (SmoothWindow / Step);

		// force HalfWindow to be odd to make sure window is centered on the point being modified
		if (HalfWindow % 2 == 0)
			HalfWindow ++;

		EndHalfWindow = 0;
		for (i = 0; i < HalfWindow; i ++) {
			AveY = 0.0;
			CellCnt = 0;
			for (j = i - EndHalfWindow; j <= i + EndHalfWindow; j ++) {
	//		for (j = i - HalfWindow; j <= i + HalfWindow; j ++) {
				if (j >= 0) {
					AveY += Y[j];
					CellCnt ++;
				}
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double) CellCnt);

			EndHalfWindow ++;
		}

		// this loop should handle only complete windows...window fits within the array bounds
		for (i = HalfWindow; i < STEPS - HalfWindow; i ++) {
			if (i == HalfWindow) {
				// first time...need to get all values
				AveY = 0.0;
				CellCnt = 0;
				for (j = i - HalfWindow; j <= i + HalfWindow; j ++) {
					if (j >= 0 && j < STEPS) {
						AveY += Y[j];
						CellCnt ++;
					}
				}
			}
			else {
				// after first time only need to change first and last values
				AveY -= Y[(i - HalfWindow) - 1];		// subtract 1 since i has been advanced by 1 since "group" was formed
				AveY += Y[(i + HalfWindow)];
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double) CellCnt);
		}

		EndHalfWindow = 0;
		for (i = STEPS - 1; i >= max(HalfWindow, STEPS - HalfWindow); i --) {
	//	for (i = STEPS - HalfWindow; i < STEPS; i ++) {
			AveY = 0.0;
			CellCnt = 0;
			for (j = i - EndHalfWindow; j <= i + EndHalfWindow; j ++) {
	//		for (j = i - HalfWindow; j <= i + HalfWindow; j ++) {
				if (j >= 0 && j < STEPS) {
					AveY += Y[j];
					CellCnt ++;
				}
			}
			SmoothX[i] = X[i];
			SmoothY[i] = AveY / ((double) CellCnt);

			EndHalfWindow ++;
		}
	}

	// figure out the signs
	int FirstMin = FALSE;
	sign[0] = -1;
	sign[STEPS - 1] = 1;
	if (!UseSmoothedCurve) {
		for (i = 0; i < STEPS; i ++) {
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
		for (i = 0; i < STEPS; i ++) {
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
	for (i = 1; i < STEPS; i ++) {
		// detect transitions from positive slope to flat or negative slope...peaks
		if (sign[i] != sign[i - 1] && sign[i - 1] == 1) {
			Modes[ModeCnt] = X[i - 1];
			ModeProb[ModeCnt] = Y[i - 1];
			ModeType[ModeCnt] = 1;

//			printf("%8lf %.8lf %.8lf 255 0 0\n", 0.0, Y[i] * 100.0, X[i]);
			ModeCnt ++;
		}

		// detect transitions from negative slope to flat or positive slope...valleys
		if (sign[i] != sign[i - 1] && sign[i - 1] == -1) {
			Modes[ModeCnt] = X[i - 1];
			ModeProb[ModeCnt] = Y[i - 1];
			ModeType[ModeCnt] = -1;

			ModeCnt ++;
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
	for (i = 0; i < ModeCnt; i ++) {
		if (ModeType[i] == 1 && (Modes[i] >= DataMinElev && Modes[i] <= DataMaxElev)) {
			MinMode = min(MinMode, Modes[i]);
			MaxMode = max(MaxMode, Modes[i]);

			ModeCount ++;
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
		if (UseSmoothedCurve) {
			for (i = 0; i < STEPS; i ++) {
				fprintf(f, "%.8lf %.8lf %i\n", SmoothY[i] * 100, SmoothX[i], sign[i]);
			}
		}
		else {
			for (i = 0; i < STEPS; i ++) {
				fprintf(f, "%.8lf %.8lf %i\n", Y[i] * 100, X[i], sign[i]);
			}
		}
		fclose(f);
	}

	// "raw" KDE values
	for (i = 0; i < STEPS; i ++) {
		printf("%8lf %.8lf %.8lf 255 255 0\n", 0.0, Y[i] * 100, X[i]);
	}

	// smoothed values
	if (UseSmoothedCurve) {
		for (i = 0; i < STEPS; i ++) {
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

