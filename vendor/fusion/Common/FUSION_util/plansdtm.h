// PlansDTM class definition
//
// PATCHSIZE controls size of DTM data blocks used by real-time Z functions.
#ifndef __PLANSDTM__
#define __PLANSDTM__
#define PATCHSIZE 10

#define		HIGHPEAKS	0
#define		LOWPEAKS	1
#define		NOPEAKS		-1

#define		SLOPE		1
#define		ASPECT		2

typedef struct {
	char	signature[21];			// file signature
	char	name[61];				// descriptive name
	float	version;				// file format version
	double	origin_x;				// model origin (LL corner)
	double	origin_y;				// model origin (LL corner)
	double	min_z;					// minimum elevation in model
	double	max_z;					// maximum elevation in model
	double	rotation;				// rotation for coordinate system...not supported
	double	column_spacing;			// column spacing
	double	point_spacing;			// row or point spacing along a column
	long	columns;				// number of columns
	long	points;					// number of rows or points in a column
	short	xy_units;				// unit code for XY: 0=feet, 1=meters, 2=other
	short	z_units;				// unit code for elevations: 0=feet, 1=meters, 2=other
	short	z_bytes;				// storage type for elevations...only int16
	short	coord_sys;				// coordinate system...format 2.0 only
	short	coord_zone;				// zone in coord system...format 2.0 only
	short	horizontal_datum;		// horizontal datum...format 3.1 and newer...0=unknown, 1=NAD27, 2=NAD83
	short	vertical_datum;			// vertical datum...format 3.1 and newer...0=unknown, 1=NGVD29, 2=NAVD88, 3=GRS80
	double	bias;					// value to be subtracted from all elevations in model...version 3.2 and newer...allows (-) elevations
} BIN_HEADER;

typedef struct {
	double x;
	double y;
} DBLCOORD;

//////////////////////////////////////////////////////////////////////////////////////

class PlansDTM {
	friend class DTM2D;
	friend class DTM3D;
public:
	BOOL ElevationsAreLoaded();
	void ChangeMinMaxElevation(double MinZ, double MaxZ);
	int ScanModelForVoidAreas(void);
	void RefreshPatch();
	void ChangeVerticalDatum(int Datum);
	void ChangeHorizontalDatum(int Datum);
	void ChangeCoordinateZone(int CoordZone);
	void ChangeCoordinateSystem(int CoordSys);
	void ChangeDescriptiveName(LPCTSTR NewName);
	void ChangeXYUnits(int Units);
	void ChangeZUnits(int Units);
	void SetInternalElevationValue(int col, int row, double value);
	double Bias();
	double RemoveBias(double Z);
	double ApplyBias(double Z);
	BOOL CreateTextureSurface(int FilterSize, int Method = 0);
	BOOL CreateTopoSurface(int Type = SLOPE);
	double GetGridElevation(double x, double y, BOOL ConvertElevation=TRUE);
	void FillHoles(int PeakRule = -1, int MaxCells = 3, double HoleThreshold = 0.0);
	void OriginalFillHoles(int PeakRule = -1, int MaxCells = 3);
	BOOL WriteModel(LPCTSTR FileName);
	BOOL UpdateModelHeader(LPCTSTR FileName);
	void MedianFilter(int FilterSize, BOOL PreserveLocalPeaks = FALSE, int PeakRule = HIGHPEAKS);
	void MovingWindowFilter(int FilterSize, double* Weights, BOOL PreserveLocalPeaks = FALSE, int PeakRule = HIGHPEAKS);
	double GetElevationConversion();
	BOOL WriteASCIICSV(LPCTSTR FileName, double Zfactor=1.0);
	BOOL WriteENVIFile(LPCTSTR FileName, LPCTSTR HeaderFileName, int Hemisphere = 0);
	BOOL WriteASCIIGrid(LPCTSTR FileName, double Zfactor = 1.0, BOOL DataIsRaster = FALSE);
	BOOL WriteSurferASCIIGrid(LPCTSTR FileName, double Zfactor = 1.0);
	BOOL WriteTriangleFile(LPCTSTR FileName, double BaseHeight = 100.0, BOOL MakeEPOB = FALSE);
	BOOL WriteASCIIXYZ(LPCTSTR FileName, double XYfactor = 1.0, double Zfactor = 1.0, BOOL OutputVoidAreas = TRUE);
	BOOL WriteASCIIXYZCSV(LPCTSTR FileName, double XYfactor = 1.0, double Zfactor = 1.0, BOOL OutputVoidAreas = TRUE, BOOL OutputHeader = TRUE);
	void SetVerticalExaggeration(double ve);
	int CoordinateZone();
	int CoordinateSystem();
	int HorizontalDatum();
	int VerticalDatum();
	float Version();
	double GetGridElevation(int col, int row);
	int GetElevationUnits();
	int GetZBytes();
	int GetXYUnits();
	void GetDescriptiveName(CString& csName);
	void GetDescriptiveName(LPSTR csName);
	// constructors
	// default sets up class only...must call Open or OpenAndRead to create model
	PlansDTM();

	// create and open model...LoadElev=TRUE...read elev data into memory...ignores UsePatch
	//						   UsePatch=TRUE...use patch access for real-time z access	
	PlansDTM(LPCSTR filename, BOOL LoadElev = FALSE, BOOL UsePatch = TRUE);

	// open model...LoadElev=TRUE...read elev data into memory...ignores UsePatch
	//			    UsePatch=TRUE...use patch access for real-time z access
	//
	// most useful when you want to try to open the model reading all elevations...
	// if this fails, try to open using patch access
	BOOL LoadElevations(LPCSTR filename, BOOL LoadElev = FALSE, BOOL UsePatch = TRUE);

	// destructors
	~PlansDTM();

	// destroy model...close file...free memory
	void Destroy(void);
	
	// access functions
	BOOL OpenAndRead(LPCSTR filename);
	BOOL Open(LPCSTR filename);

	// information functions
	long	Columns(void);
	long	Rows(void);
	double	ColumnSpacing(void);
	double	PointSpacing(void);
	double	Width(void);
	double	Height(void);
	double	MinElev(void);
	double	MaxElev(void);
	double	OriginX(void);
	double	OriginY(void);
	double	Rotation(void);

	// test for valid model
	BOOL	IsValid(void);

	// function to get elevation for arbitrary XY
	double	InterpolateElev(double x, double y, BOOL ConvertElevation = TRUE);

	// function to get highest point in the bounding cell...used when DTM contains point counts
	double	GetHighPointInBoundingCell(double x, double y, BOOL ConvertElevation = TRUE);

	double	GetClosestPoint(double x, double y, BOOL ConvertElevation = TRUE);

	// create a level DTM
	BOOL CreateDummyModel(CString FileName, double OriginX, double OriginY, int Cols, int Rows, double ColSpacing, double RowSpacing, int XYunits, int Zunits, int Elevation);

	// create a new model with elevations in memory
	BOOL CreateNewModel(double OriginX, double OriginY, int Cols, int Rows, double ColSpacing, double RowSpacing, int XYunits, int Zunits, int Zbytes, int CoordSystem, int CoordZone, int Hdatum, int Zdatum, int Elevation = -1.0);

private:
	double VerticalExaggeration;
	double ElevationConversion;
	BOOL				Valid;						// flag indicating DTM is loaded

	BIN_HEADER			Header;						// dtm header from file
	void**				lpsElevData;				// pointer to elevation data
	BOOL				HaveElevations;				// flag indicating elevations are in memory

	char				DTMFileName[512];			// DTM file name

	FILE*				modelfile;					// DTM file handle...when needed

	// patch related stuff
	long				patch_llx;
	long				patch_lly;
	long				cell_llx;
	long				cell_lly;

	void**				lpsDTMPatch;				// pointer to patch
	BOOL				UsingPatchElev;				// flag indicating use pf elev patch logic

	BOOL	OpenModelFile(void);
	void	CloseModelFile(void);

	BOOL	ReadHeader(LPCSTR filename);
	
	BOOL	InitPatchElev(void);
	void	TermPatchElev(void);
	double	InterpolateElevPatch(double x, double y);
	double	GetHighPointInBoundingCellPatch(double x, double y);
	
	int		SeekToPoint(long profile, long point);
	int		SeekToProfile(long profile);
	
	double	ReadColumnValue(void *col, int row);
	double	ReadInternalElevationValue(int col, int row, BOOL FixVoid = FALSE);
	double	ReadInternalPatchElevationValue(int col, int row, BOOL FixVoid = FALSE);
	void	SetInternalPatchElevationValue(int col, int row, double value);

	double	LoadDTMPoint(long profile, long point);
	BOOL	LoadDTMPartialProfile(long profile, long start_pt, long stop_pt, void *dtm_array);
	BOOL	LoadDTMProfile(long profile, void *dtm_array);
	BOOL	LoadDTMPatch(long ll_col, long ll_pt);

	int **	AllocateIntArray(int Columns, int Rows);
	void	DeallocateIntArray(int** lpsData, int Columns);

protected:
	void FreeElevationMemory(void**& ElevData, int Columns);
	BOOL AllocateElevationMemory(void**& ElevData, int DataType, int Columns, int Rows, double InitialValue = -1.0);
};
#endif