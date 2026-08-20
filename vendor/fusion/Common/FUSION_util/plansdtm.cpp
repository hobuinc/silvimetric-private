// PlansDTM class 
//
// 4/4/2007
// added functions to allocate and free elevation memory using void** to support multiple types
// also modified FillHoles(), MedianFilter(), and MovingWindowFilter() to use the new functions
//
// previously, these functions used double types for all models regardless of the elevation storage
// type of the model...required more memory
//
// 11/18/2008
// added logic to FillHoles that does not extend the surface beyond the original data extent. surfaces
// that were "filled" could end up with bad data around their edges as good data were smeared towards the 
// edge of the coverage area. The new logic was taken from GridSurfaceCreate
//
// 8/12/2009
// added exception trapping when allocating memory for elevation data. this should make loading a model into
// memory much more robust. also added the same exception handling to AllocateIntArray()
//
// 11/24/2009
// Corrected problem when allocating memory for models using short values for elevations. The array of column
// pointers was being allocated correctly but not the individual columns. Problem was that "Columns" was being used
// for the size of both allocation attempts. In models where the number of columns was greater than the number of
// rows, there was no problem. In the opposite case, the memory block for the elevations wass too small. Symptom
// would be a major crash.
//
// 12/9/2009
// Added option when saving DTM to ASCII raster format to control the location of the origin. Since I use the format
// for both point sample data and raster data, I needed a way to make the ASCII raster files with the correct
// origin (either lower left corner of lower left cell or center of lower left cell).
//
// 5/23/2013
// Changed precision when writing DTM to ASCII raster format from 4 to 6 decimal digits. This makes outputs match those from 
// other programs in the FUSION/LTK suite. Also changed the precision of NODATA values to use no decimal digits. This should make 
// output files smaller in some cases.
//
// 4/21/2014
// Modified logic used when writing ASCII raster format for DTM to allow point access. This uncovered some problems in the logic
// in the DTM class related to reading individual points from a file. I don't think I have used the LoadDTMPoint() function before.
// Fixed the problems and the WriteASCIIGrid function is now working when the model cannot be loaded into memory. On a test (forced),
// the performance was 2X longer than when the model could be loaded into memory.
//
//	10/1/2014
//	Modified the checks on available memory to use the amount of virtual memory instead of physical memory. This should improve stability.
//
//	2/26/2015
//	Additional work on memory management. Removed the test of available memory in AllocateElevationMemory() and just catch the exception
//	thrown when an allocation doesn't work. Turns out Windows doesn't give a good value for the largest block of memory that might be
//	successfully allocated.
//
//	9/25/2015
//	Added check in Open() to verify that the file is actually a DTM file. Previously you could open just about any file and have Open()
//	return success (provided you were not reading elevations or setting up for patch access)
//
//	11/2/2015
//	Modified the logic used to fill holes in surfaces so that it requires values on opposite sides of the hole before any filling occurs.
//	This helps prevent "filling" beyond the extent of valid data when the extent is a non-geometrically closed polygon.
//
//	This change doesn't completely prevent "filling" in areas that don't have data but helps. Different programs use different search radii
//	when filling holes. In general, the smaller the search radii, the less expansion of surfaces into areas with no data. However, the radii
//	are expressed in cells so DTMs with large cells can still be looking over large distances to find valid grid values to use to interpolate
//	a value to fill a hole. I have started to add a /nofill option to several of the programs to provide more control over the filling process.
//
//	9/26/2016
//	Modified the logic used to fill holes in models to use a threshold value to define hole elevations. I ran into problems with a data set
//	where the ground model was below some of the point data. The result was that when you normalized the point elevations, you end up with negative
//	values for the point height. If you the create a new surface using the point heights, the old hole filling logic did wierd things to the areas
//	with negative heights. I added a HoleThreshold parameter to the FillHoles() function to define the minimum height for holes. the default value
//	produces the same behavior as the original version of the function.
//
//	3/4/2021
//	Made changes to deal with files larger than 2Gb. Specifically columns with more than 2Gb of data (unlikely). Changes made in 
//		SeekToPoint
//		SeekToProfile
//		WriteENVIFile
//
//	8/5/2022
//	Fixed a memory leak related to initializing the patch logic for a non-existent model. The memory for the patch was being allocated using new
//	but the flag indicating that the patch memory was active was not being set since the model file couldn't be opened. The fix involved moving the 
//	setting of the flag in InitPatchElev() up a couple of lines so the flag was set prior to exiting the function when loading the first patch
//	of data failed.
//
//	9/6/2023
//	Made additional changes to deal with files larger than 2Gb. Changed variable type used for fseek() calls to an 8-bit integer. Changes
//	from 3/4/2023 didn't include a change to the variable type from a 4-byte signed integer to an 8-bit value so overflow would happen with 
//	large files.
//
#include <stdafx.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#include "plansdtm.h"

int compelev(const void *arg1, const void *arg2)
{
	if (*((double*) arg1) < *((double*) arg2))
		return(-1);
	else if (*((double*) arg1) > *((double*) arg2))
		return(1);
	else
		return(0);
}

PlansDTM::PlansDTM()
{
	Valid = FALSE;
	HaveElevations = FALSE;
	UsingPatchElev = FALSE;
	ElevationConversion = 1.0;
	VerticalExaggeration = 1.0;
	DTMFileName[0] = '\0';

	lpsElevData = NULL;
	lpsDTMPatch = NULL;
}

PlansDTM::PlansDTM(LPCSTR filename, BOOL LoadElev, BOOL UsePatch)
{
	Valid = FALSE;
	HaveElevations = FALSE;
	UsingPatchElev = FALSE;
	ElevationConversion = 1.0;
	VerticalExaggeration = 1.0;
	DTMFileName[0] = '\0';

	lpsElevData = NULL;
	lpsDTMPatch = NULL;

	if (!LoadElev) {
		Open(filename);
		if (Valid && UsePatch)
			InitPatchElev();
	}
	else
		OpenAndRead(filename);
}

BOOL PlansDTM::LoadElevations(LPCSTR filename, BOOL LoadElev, BOOL UsePatch)
{
	if (Valid)
		Destroy();

	if (!LoadElev) {
		Open(filename);
		if (UsePatch)
			InitPatchElev();
	}
	else
		OpenAndRead(filename);

	return(Valid);
}

PlansDTM::~PlansDTM()
{
	Destroy();
}

void PlansDTM::Destroy(void)
{
	if (HaveElevations) {
		FreeElevationMemory(lpsElevData, Header.columns);
//		for (int i = 0; i < Header.columns; i ++)
//			delete [] lpsElevData[i];
//
//		delete [] lpsElevData;
//		lpsElevData = NULL;
	}

	if (UsingPatchElev) {
		TermPatchElev();
	}

	Valid = FALSE;
	HaveElevations = FALSE;
	UsingPatchElev = FALSE;
}

double PlansDTM::Width(void)
{
	if (Valid)
		return((double) (Header.columns - 1l) * Header.column_spacing);
	else
		return(0.0);
}

double PlansDTM::Rotation(void)
{
	if (Valid)
		return(Header.rotation);
	else
		return(0.0);
}

double PlansDTM::Height(void)
{
	if (Valid)
		return((double) (Header.points - 1l) * Header.point_spacing);
	else
		return(0.0);
}

double PlansDTM::MinElev(void)
{
	if (Valid)
		return(Header.min_z);
	else
		return(0.0);
}

double PlansDTM::MaxElev(void)
{
	if (Valid)
		return(Header.max_z);
	else
		return(0.0);
}

double PlansDTM::OriginX(void)
{
	if (Valid)
		return(Header.origin_x);
	else
		return(0.0);
}

double PlansDTM::OriginY(void)
{
	if (Valid)
		return(Header.origin_y);
	else
		return(0.0);
}

long PlansDTM::Columns(void)
{
	if (Valid)
		return(Header.columns);
	else
		return(0);
}

long PlansDTM::Rows(void)
{
	if (Valid)
		return(Header.points);
	else
		return(0);
}

double PlansDTM::ColumnSpacing(void)
{
	if (Valid)
		return(Header.column_spacing);
	else
		return(0);
}

double PlansDTM::PointSpacing(void)
{
	if (Valid)
		return(Header.point_spacing);
	else
		return(0);
}

BOOL PlansDTM::IsValid(void)
{
	return(Valid);
}

BOOL PlansDTM::OpenAndRead(LPCSTR filename)
{
	// open model, read header, read data
	FILE		*modelfile = NULL;
	long		i;

	if (Valid)
		Destroy();

	// read the PlansDTM header
	if (!ReadHeader(filename))
		return(FALSE);

	strcpy(DTMFileName, filename);

	// open PlansDTM file
	if (OpenModelFile()) {
		// allocate memory for elevation data
		if (!AllocateElevationMemory(lpsElevData, Header.z_bytes, Header.columns, Header.points)) {
			Valid = FALSE;
			HaveElevations = FALSE;
			UsingPatchElev = FALSE;
			CloseModelFile();
			return(Valid);
		}
/*		switch (Header.z_bytes) {
			case 0:		// short
			default:
				// allocate memory for elevation data
				lpsElevData = (void**) new short* [Header.columns];

				for (i = 0; i < Header.columns; i ++)
					lpsElevData[i] = (void*) new short [Header.points];
				break;
			case 1:		// int
				// allocate memory for elevation data
				lpsElevData = (void**) new int* [Header.columns];

				for (i = 0; i < Header.columns; i ++)
					lpsElevData[i] = (void*) new int [Header.points];
				break;
			case 2:		// float
				// allocate memory for elevation data
				lpsElevData = (void**) new float* [Header.columns];

				for (i = 0; i < Header.columns; i ++)
					lpsElevData[i] = (void*) new float [Header.points];
				break;
			case 3:		// double
				// allocate memory for elevation data
				lpsElevData = (void**) new double* [Header.columns];

				for (i = 0; i < Header.columns; i ++)
					lpsElevData[i] = (void*) new double [Header.points];
				break;
		}
*/
		
		// read data values
		for (i = 0; i < Header.columns; i ++) 
			LoadDTMProfile(i, lpsElevData[i]);

		CloseModelFile();
	}
	else 
		return(FALSE);

	Valid = TRUE;
	HaveElevations = TRUE;
	UsingPatchElev = FALSE;
	
	return(Valid);
}

BOOL PlansDTM::OpenModelFile(void)
{
	modelfile = fopen(DTMFileName, "rb");

	if (modelfile != (FILE *) NULL)
		return(TRUE);

	return(FALSE);
}

void PlansDTM::CloseModelFile(void)
{
	fclose(modelfile);
}

BOOL PlansDTM::Open(LPCSTR filename)
{
	// open model, read header

	if (Valid)
		Destroy();

	// read the PlansDTM header
	Valid = ReadHeader(filename);

	strcpy(DTMFileName, filename);

	HaveElevations = FALSE;

	return(Valid);
}

BOOL PlansDTM::ReadHeader(LPCSTR filename)
{
	FILE*		modelfile = NULL;
	BOOL		retcode = FALSE;

	if ((modelfile = fopen(filename, "rb")) == NULL)
		return(retcode);

	// assume model is good
	retcode = TRUE;

	// model is open
    fread(&Header.signature, sizeof(char), 21, modelfile);

	// check signature
	if (strcmp(Header.signature, "PLANS-PC BINARY .DTM") != 0) {
		fclose (modelfile);
		retcode = FALSE;
		return(retcode);
	}

    fread(&Header.name, sizeof(char), 61, modelfile);
    fread(&Header.version, sizeof(float), 1, modelfile);
    fread(&Header.origin_x, sizeof(double), 1, modelfile);
    fread(&Header.origin_y, sizeof(double), 1, modelfile);
    fread(&Header.min_z, sizeof(double), 1, modelfile);
    fread(&Header.max_z, sizeof(double), 1, modelfile);
    fread(&Header.rotation, sizeof(double), 1, modelfile);
    fread(&Header.column_spacing, sizeof(double), 1, modelfile);
    fread(&Header.point_spacing, sizeof(double), 1, modelfile);
    fread(&Header.columns, sizeof(long), 1, modelfile);
    fread(&Header.points, sizeof(long), 1, modelfile);
    fread(&Header.xy_units, sizeof(short), 1, modelfile);
    fread(&Header.z_units, sizeof(short), 1, modelfile);
    fread(&Header.z_bytes, sizeof(short), 1, modelfile);

	// read coordinate system info
	if (Header.version >= 2.0f) {
		fread(&Header.coord_sys, sizeof(short), 1, modelfile);
		fread(&Header.coord_zone, sizeof(short), 1, modelfile);
	}
	else {
		Header.coord_sys = 0;
		Header.coord_zone = 0;
	}

	// read datum info
	if (Header.version >= 3.1f) {
		fread(&Header.horizontal_datum, sizeof(short), 1, modelfile);
		fread(&Header.vertical_datum, sizeof(short), 1, modelfile);
	}
	else {
		Header.horizontal_datum = 0;
		Header.vertical_datum = 0;
	}

	// read bias value
	if (Header.version >= 3.2f) {
		fread(&Header.bias, sizeof(double), 1, modelfile);
	}
	else {
		Header.bias = 0.0;
	}

	fclose (modelfile);

	// set elevation conversion factor
	if (Header.xy_units == 0 && Header.z_units == 1)
		ElevationConversion = 3.28084;
	else if (Header.xy_units == 1 && Header.z_units == 0)
		ElevationConversion = 0.3048;

	return(retcode);
}

double PlansDTM::InterpolateElev(double x, double y, BOOL ConvertElevation)
{
	// 6/15/2004 changed conversion used when different units for XY and Z...old code did not give correct elevation
	// change invloved converting the min_z value in the model (last term of conversion stmt)
	static long r, c;
	static double llz, lrz, urz, ulz, elev1, elev2, elev, nx, ny;
	static double dummy;

	if (!Valid)
		return(-1.0);

	// calculate row and column
	c = (long) floor((x - Header.origin_x) / Header.column_spacing);
	r = (long) floor((y - Header.origin_y) / Header.point_spacing);

	if (c < 0 || c > (Header.columns - 1l) || r < 0 || r > (Header.points - 1l)) {
		return(-32767.0);
	}

	// if we have elevations loaded into memory, simply compute interpolated elevation
	if (HaveElevations) {
		llz = ReadInternalElevationValue(c, r);
//		llz = (double) ((double**) lpsElevData)[c] [r];

		// check to see that point is not on right edge of PlansDTM
		if (c == Header.columns - 1)
			lrz = llz;
		else
			lrz = ReadInternalElevationValue(c + 1, r);
//			lrz = (double) lpsElevData[c + 1] [r];

		// check to see that point is not on top edge of PlansDTM
		if (r == Header.points - 1)
			ulz = llz;
		else
			ulz = ReadInternalElevationValue(c, r + 1);
//			ulz = (double) lpsElevData[c] [r + 1];

		// check to see that point is not on upper right corner of PlansDTM
		if ((c == Header.columns - 1) && (r == Header.points - 1))
			urz = llz;
		else {
			if (c == Header.columns - 1)
				urz = ulz;
			else if (r == Header.points - 1)
				urz = lrz;
			else
				urz = ReadInternalElevationValue(c + 1, r + 1);
//				urz = (double) lpsElevData[c + 1] [r + 1];
		}

		// calculate normalized coordinate...relative to lower left of cell
		// cell is 1 unit on a side regardless of actual dimensions
		nx = modf((x - (Header.origin_x)) / Header.column_spacing, &dummy);
		ny = modf((y - (Header.origin_y)) / Header.point_spacing, &dummy);

		// check to see if point is on a cell corner
		if (nx == 0.0) {
			if (ny == 0.0) {
				elev = llz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
			if (ny == 1.0) {
				elev = ulz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
		}
		if (nx == 1.0) {
			if (ny == 0.0) {
				elev = lrz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
			if (ny == 1.0) {
				elev = urz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
		}

		// look for NULL elevation (< 0) 
		if (llz < 0 || lrz < 0 || ulz < 0 || urz < 0) {
			return(-1.0);
		}
		else {
			// interpolate using vertical sides of cell first 
			elev1 = llz + (ulz - llz) * ny;
			elev2 = lrz + (urz - lrz) * ny;
			elev = elev1 + (elev2 - elev1) * nx;
			if (ConvertElevation)
				elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
			return(elev);
		}
	}
	else {
		// get elevations from disk and interpolate
		elev = InterpolateElevPatch(x, y);
		if (ConvertElevation)
			elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
		return(elev);
	}
}

double PlansDTM::GetHighPointInBoundingCell(double x, double y, BOOL ConvertElevation)
{
	// 6/15/2004 changed conversion used when different units for XY and Z...old code did not give correct elevation
	// change invloved converting the min_z value in the model (last term of conversion stmt)
	static long r, c;
	static double llz, lrz, urz, ulz, elev1, elev2, elev, nx, ny;
	static double dummy;

	if (!Valid)
		return(-1.0);

	// calculate row and column
	c = (long) floor((x - Header.origin_x) / Header.column_spacing);
	r = (long) floor((y - Header.origin_y) / Header.point_spacing);

	if (c < 0 || c > (Header.columns - 1l) || r < 0 || r > (Header.points - 1l)) {
		return(-32767.0);
	}

	// if we have elevations loaded into memory, simply compute interpolated elevation
	if (HaveElevations) {
		llz = ReadInternalElevationValue(c, r);
//		llz = (double) ((double**) lpsElevData)[c] [r];

		// check to see that point is not on right edge of PlansDTM
		if (c == Header.columns - 1)
			lrz = llz;
		else
			lrz = ReadInternalElevationValue(c + 1, r);
//			lrz = (double) lpsElevData[c + 1] [r];

		// check to see that point is not on top edge of PlansDTM
		if (r == Header.points - 1)
			ulz = llz;
		else
			ulz = ReadInternalElevationValue(c, r + 1);
//			ulz = (double) lpsElevData[c] [r + 1];

		// check to see that point is not on upper right corner of PlansDTM
		if ((c == Header.columns - 1) && (r == Header.points - 1))
			urz = llz;
		else {
			if (c == Header.columns - 1)
				urz = ulz;
			else if (r == Header.points - 1)
				urz = lrz;
			else
				urz = ReadInternalElevationValue(c + 1, r + 1);
//				urz = (double) lpsElevData[c + 1] [r + 1];
		}

		// calculate normalized coordinate...relative to lower left of cell
		// cell is 1 unit on a side regardless of actual dimensions
		nx = modf((x - (Header.origin_x)) / Header.column_spacing, &dummy);
		ny = modf((y - (Header.origin_y)) / Header.point_spacing, &dummy);

		// check to see if point is on a cell corner
		if (nx == 0.0) {
			if (ny == 0.0) {
				elev = llz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
			if (ny == 1.0) {
				elev = ulz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
		}
		if (nx == 1.0) {
			if (ny == 0.0) {
				elev = lrz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
			if (ny == 1.0) {
				elev = urz;
				if (ConvertElevation)
					elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
				return(elev);
			}
		}

		// set elevation to -1 then look for maximum value from bounding cell
		elev = -1.0;

		elev = max(elev, llz);
		elev = max(elev, lrz);
		elev = max(elev, ulz);
		elev = max(elev, urz);

		if (elev >= 0.0 && ConvertElevation)
			elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;

		return(elev);
	}
	else {
		// get elevations from disk and interpolate
		elev = GetHighPointInBoundingCellPatch(x, y);
		if (ConvertElevation)
			elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;
		return(elev);
	}
}

double PlansDTM::GetClosestPoint(double x, double y, BOOL ConvertElevation)
{
	static long r, c;
	static double elev;
	long cell_llx, cell_lly;

	if (!Valid)
		return(-1.0);

	// calculate row and column...add half of the cell size so we get the closest grid point
	c = (long) floor(((x + Header.column_spacing / 2.0) - Header.origin_x) / Header.column_spacing);
	r = (long) floor(((y + Header.point_spacing / 2.0) - Header.origin_y) / Header.point_spacing);

	if (HaveElevations) {
		elev = GetGridElevation(static_cast<int>(c), static_cast<int>(r));
	}
	else {
		// use patch logic

		// check to make sure the point is within the dtm area, if not flag Z and return
		if (c < 0 || c > (Header.columns - 1l) || r < 0 || r > (Header.points - 1l)) {
			return(-32767.0);
		}

		// check to see if lower left corner is outside current patch 
		if (c < patch_llx || c > (patch_llx + PATCHSIZE - 1l) || r < patch_lly || r > (patch_lly + PATCHSIZE - 1l)) {
			if (!LoadDTMPatch(max(0l, c - 5l), max(0l, r - 5l))) {
				return(-32767.0);
			}
		}

		// compute the local patch coordinates
		cell_llx = c - patch_llx;
		cell_lly = r - patch_lly;

		elev = ReadInternalPatchElevationValue(cell_llx, cell_lly);
	}

	if (ConvertElevation)
		elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;

	return(elev);
}

int PlansDTM::SeekToPoint(long profile, long point)
{
	__int64 byte_offset = 0;

	switch (Header.z_bytes) {
		case 0:		// short
		default:
			byte_offset = (__int64)200l + ((((__int64)profile * (__int64)Header.points) + (__int64)point) * sizeof(short));
//			byte_offset = 200l + (profile * (Header.points * sizeof(short))) + (point * sizeof(short));
			break;
		case 1:		// int
			byte_offset = (__int64)200l + ((((__int64)profile * (__int64)Header.points) + (__int64)point) * sizeof(int));
//			byte_offset = 200l + (profile * (Header.points * sizeof(int))) + (point * sizeof(int));
			break;
		case 2:		// float
			byte_offset = (__int64)200l + ((((__int64)profile * (__int64)Header.points) + (__int64)point) * sizeof(float));
//			byte_offset = 200l + (profile * (Header.points * sizeof(float))) + (point * sizeof(float));
			break;
		case 3:		// double
			byte_offset = (__int64)200l + ((((__int64)profile * (__int64)Header.points) + (__int64)point) * sizeof(double));
//			byte_offset = 200l + (profile * (Header.points * sizeof(double))) + (point * sizeof(double));
			break;
	}
//	byte_offset = 200l + (profile * (Header.points * sizeof(short))) + (point * sizeof(short));

	return(_fseeki64(modelfile, byte_offset, SEEK_SET));
//	return(fseek(modelfile, byte_offset, SEEK_SET));
}

int PlansDTM::SeekToProfile(long profile)
{
	__int64 byte_offset = 0;

	switch (Header.z_bytes) {
		case 0:		// short
		default:
			byte_offset = (__int64)200l + (__int64)profile * ((__int64)Header.points * sizeof(short));
			//			byte_offset = 200l + profile * (Header.points * sizeof(short));
			break;
		case 1:		// int
			byte_offset = (__int64)200l + (__int64)profile * ((__int64)Header.points * sizeof(int));
//			byte_offset = 200l + profile * (Header.points * sizeof(int));
			break;
		case 2:		// float
			byte_offset = (__int64)200l + (__int64)profile * ((__int64)Header.points * sizeof(float));
//			byte_offset = 200l + profile * (Header.points * sizeof(float));
			break;
		case 3:		// double
			byte_offset = (__int64)200l + (__int64)profile * ((__int64)Header.points * sizeof(double));
//			byte_offset = 200l + profile * (Header.points * sizeof(double));
			break;
	}
//	byte_offset = 200l + profile * (Header.points * sizeof(short));

	return(_fseeki64(modelfile, byte_offset, SEEK_SET));
//	return(fseek(modelfile, byte_offset, SEEK_SET));
}

double PlansDTM::LoadDTMPoint(long profile, long point)
{
	short selevation;
	int ielevation;
	float felevation;
	double delevation;

	if (SeekToPoint(profile, point))
		return(-32767.0);

	switch (Header.z_bytes) {
		case 0:		// short
		default:
			if (fread(&selevation, sizeof(short), 1, modelfile) != 1)
				return(-32767.0);
			delevation = (double) selevation;
			break;
		case 1:		// int
			if (fread(&ielevation, sizeof(int), 1, modelfile) != 1)
				return(-32767.0);
			delevation = (double) ielevation;
			break;
		case 2:		// float
			if (fread(&felevation, sizeof(float), 1, modelfile) != 1)
				return(-32767.0);
			delevation = (double) felevation;
			break;
		case 3:		// double
			if (fread(&delevation, sizeof(double), 1, modelfile) != 1)
				return(-32767.0);
			break;
	}
//	if (fread(&elevation, sizeof(short), 1, modelfile) != 1)
//		return(-32767);

	return(delevation);
}

BOOL PlansDTM::LoadDTMProfile(long profile, void *dtm_array)
{
	if (SeekToProfile(profile))
		return(-1);

	switch (Header.z_bytes) {
		case 0:		// short
		default:
			if (fread(dtm_array, sizeof(short), (size_t) Header.points, modelfile) != (size_t) Header.points)
				return(-1);
			break;
		case 1:		// int
			if (fread(dtm_array, sizeof(int), (size_t) Header.points, modelfile) != (size_t) Header.points)
				return(-1);
			break;
		case 2:		// float
			if (fread(dtm_array, sizeof(float), (size_t) Header.points, modelfile) != (size_t) Header.points)
				return(-1);
			break;
		case 3:		// double
			if (fread(dtm_array, sizeof(double), (size_t) Header.points, modelfile) != (size_t) Header.points)
				return(-1);
			break;
	}
//	if (fread(dtm_array, sizeof(short), (size_t) Header.points, modelfile) != (size_t) Header.points)
//		return(-1);

	return(0);
}

BOOL PlansDTM::LoadDTMPartialProfile(long profile, long start_pt, long stop_pt, void *dtm_array)
{
	if (SeekToPoint(profile, start_pt))
		return(FALSE);

	switch (Header.z_bytes) {
		case 0:		// short
		default:
			if (fread(dtm_array, sizeof(short), (size_t) (stop_pt - start_pt + 1l), modelfile) != (size_t) (stop_pt - start_pt + 1l))
				return(FALSE);
			break;
		case 1:		// int
			if (fread(dtm_array, sizeof(int), (size_t) (stop_pt - start_pt + 1l), modelfile) != (size_t) (stop_pt - start_pt + 1l))
				return(FALSE);
			break;
		case 2:		// float
			if (fread(dtm_array, sizeof(float), (size_t) (stop_pt - start_pt + 1l), modelfile) != (size_t) (stop_pt - start_pt + 1l))
				return(FALSE);
			break;
		case 3:		// double
			if (fread(dtm_array, sizeof(double), (size_t) (stop_pt - start_pt + 1l), modelfile) != (size_t) (stop_pt - start_pt + 1l))
				return(FALSE);
			break;
	}
//	if (fread(dtm_array, sizeof(short), (size_t) (stop_pt - start_pt + 1l), modelfile) != (size_t) (stop_pt - start_pt + 1l))
//		return(FALSE);

	return(TRUE);
}

void PlansDTM::RefreshPatch()
{
	if (UsingPatchElev) {
		LoadDTMPatch(patch_llx, patch_lly);
	}
}

BOOL PlansDTM::LoadDTMPatch(long ll_col, long ll_pt)
{
	// set entire patch to -1
	for (int k = 0; k < (PATCHSIZE + 1); k ++) {
		for (int l = 0; l < (PATCHSIZE + 1); l ++) {
			SetInternalPatchElevationValue(k, l, -1.0);
//			lpsDTMPatch[k][l] = -1;
		}
	}

	if (OpenModelFile()) {
		for (long i = ll_col; i <= min((ll_col + PATCHSIZE), (Header.columns - 1l)); i ++) {
			if (!LoadDTMPartialProfile(i, ll_pt, min((ll_pt + PATCHSIZE), (Header.points - 1l)), lpsDTMPatch[i - ll_col]))
				return(FALSE);
		}

		// set the patch origin
		patch_llx = ll_col;
		patch_lly = ll_pt;

		CloseModelFile();
	}
	else {
		patch_llx = -1;
		patch_lly = -1;

		return(FALSE);
	}

	return(TRUE);
}

BOOL PlansDTM::InitPatchElev(void)
{
	long i;
	switch (Header.z_bytes) {
		case 0:		// short
		default:
			// allocate memory for elevation data
			lpsDTMPatch = (void**) new short* [PATCHSIZE + 1];

			for (i = 0; i < (PATCHSIZE + 1); i ++)
				lpsDTMPatch[i] = (void*) new short [PATCHSIZE + 1];
			break;
		case 1:		// int
			// allocate memory for elevation data
			lpsDTMPatch = (void**) new int* [PATCHSIZE + 1];

			for (i = 0; i < (PATCHSIZE + 1); i ++)
				lpsDTMPatch[i] = (void*) new int [PATCHSIZE + 1];
			break;
		case 2:		// float
			// allocate memory for elevation data
			lpsDTMPatch = (void**) new float* [PATCHSIZE + 1];

			for (i = 0; i < (PATCHSIZE + 1); i ++)
				lpsDTMPatch[i] = (void*) new float [PATCHSIZE + 1];
			break;
		case 3:		// double
			// allocate memory for elevation data
			lpsDTMPatch = (void**) new double* [PATCHSIZE + 1];

			for (i = 0; i < (PATCHSIZE + 1); i ++)
				lpsDTMPatch[i] = (void*) new double [PATCHSIZE + 1];
			break;
	}
//	// allocate memory for elevation data
//	lpsDTMPatch = new short* [PATCHSIZE + 1];
//
//	for (int i = 0; i < (PATCHSIZE + 1); i ++)
//		lpsDTMPatch[i] = new short [PATCHSIZE + 1];

	UsingPatchElev = TRUE;

	// read a patch of the dtm data
	if (!LoadDTMPatch(0, 0)) {
		return(FALSE);
	}

	return(TRUE);
}

void PlansDTM::TermPatchElev(void)
{
	if (UsingPatchElev) {
		for (int i = 0; i < (PATCHSIZE + 1); i ++)
			delete [] lpsDTMPatch[i];

		delete [] lpsDTMPatch;
		lpsDTMPatch = NULL;

		// set patch corner to -1,-1
		patch_llx = -1;
		patch_lly = -1;

		UsingPatchElev = FALSE;
	}
}

double PlansDTM::InterpolateElevPatch(double x, double y)
{
	long ll_col, ll_pt;
	long cell_llx, cell_lly;
	double llz, lrz, urz, ulz, elev1, elev2, nx, ny;
	double dummy;

	/* calculate the lower left grid intersection */
	ll_col = (long) floor((x - Header.origin_x) / Header.column_spacing);
	ll_pt = (long) floor((y - Header.origin_y) / Header.point_spacing);

	/* check to make sure the point is within the dtm area, if not flag Z and return */
	if (ll_col < 0 || ll_col > (Header.columns - 1l) || ll_pt < 0 || ll_pt > (Header.points - 1l)) {
		return(-32767.0);
	}

	/* check to see if lower left corner is outside current patch */
	if (ll_col < patch_llx || ll_col > (patch_llx + PATCHSIZE - 1l) || ll_pt < patch_lly || ll_pt > (patch_lly + PATCHSIZE - 1l)) {
		if (!LoadDTMPatch(max(0l, ll_col - 5l), max(0l, ll_pt - 5l))) {
			return(-32767.0);
		}
	}

	/* check for point on right edge of dtm */
	if (ll_col == Header.columns - 1l) {
		if (x > (Header.origin_x + (Header.columns - 1l) * Header.column_spacing)) {
			/* do a second test */
			if (x > (Header.origin_x + (Header.columns - 1l) * Header.column_spacing + 0.001)) {
				return(-32767.0);
			}
		}
	}

	/* check for point on top edge of dtm */
	if (ll_pt == Header.points - 1l) {
		if (y > (Header.origin_y + (Header.points - 1l) * Header.point_spacing)) {
			/* do a second test */
			if (y > (Header.origin_y + (Header.points - 1l) * Header.point_spacing + 0.001)) {
				return(-32767.0);
			}
		}
	}

	/* compute the local patch coordinates */
	cell_llx = ll_col - patch_llx;
	cell_lly = ll_pt - patch_lly;

	llz = ReadInternalPatchElevationValue(cell_llx, cell_lly);
//	llz = (double) lpsDTMPatch[cell_llx] [cell_lly];

	/* check to see that point is not on right edge of dtm */
	if (ll_col == Header.columns - 1l)
		lrz = llz;
	else
		lrz = ReadInternalPatchElevationValue(cell_llx + 1, cell_lly);
//		lrz = (double) lpsDTMPatch[cell_llx + 1l] [cell_lly];

	/* check to see that point is not on top edge of dtm */
	if (ll_pt == Header.points - 1l)
		ulz = llz;
	else
		ulz = ReadInternalPatchElevationValue(cell_llx, cell_lly + 1);
//		ulz = (double) lpsDTMPatch[cell_llx] [cell_lly + 1l];

	/* check to see that point is not on upper right corner of dtm */
	if ((ll_col == Header.columns - 1l) && (ll_pt == Header.points - 1l))
		urz = llz;
	else {
		if (ll_col == Header.columns - 1)
			urz = ulz;
		else if (ll_pt == Header.points - 1)
			urz = lrz;
		else
			urz = ReadInternalPatchElevationValue(cell_llx + 1, cell_lly + 1);
//			urz = (double) lpsDTMPatch[cell_llx + 1l] [cell_lly + 1l];
	}

	// calculate normalized coordinate...relative to lower left of cell
	// cell is 1 unit on a side regardless of actual dimensions
	nx = modf((x - (Header.origin_x)) / Header.column_spacing, &dummy);
	ny = modf((y - (Header.origin_y)) / Header.point_spacing, &dummy);

	// check to see if point is on a cell corner
	if (nx == 0.0 && ny == 0.0) {
		return(llz);
	}
	if (nx == 1.0 && ny == 0.0) {
		return(lrz);
	}
	if (nx == 1.0 && ny == 1.0) {
		return(urz);
	}
	if (nx == 0.0 && ny == 1.0) {
		return(ulz);
	}

	// look for NULL elevation (< 0) 
	if (llz < 0 || lrz < 0 || ulz < 0 || urz < 0) {
		return(-1.0);
	}
	else {
		// interpolate using vertical sides of cell first 
		elev1 = llz + (ulz - llz) * ny;
		elev2 = lrz + (urz - lrz) * ny;
		return(elev1 + (elev2 - elev1) * nx);
	}
}
double PlansDTM::GetHighPointInBoundingCellPatch(double x, double y)
{
	long ll_col, ll_pt;
	long cell_llx, cell_lly;
	double llz, lrz, urz, ulz, elev, nx, ny;
	double dummy;

	/* calculate the lower left grid intersection */
	ll_col = (long) floor((x - Header.origin_x) / Header.column_spacing);
	ll_pt = (long) floor((y - Header.origin_y) / Header.point_spacing);

	/* check to make sure the point is within the dtm area, if not flag Z and return */
	if (ll_col < 0 || ll_col > (Header.columns - 1l) || ll_pt < 0 || ll_pt > (Header.points - 1l)) {
		return(-32767.0);
	}

	/* check to see if lower left corner is outside current patch */
	if (ll_col < patch_llx || ll_col > (patch_llx + PATCHSIZE - 1l) || ll_pt < patch_lly || ll_pt > (patch_lly + PATCHSIZE - 1l)) {
		if (!LoadDTMPatch(max(0l, ll_col - 5l), max(0l, ll_pt - 5l))) {
			return(-32767.0);
		}
	}

	/* check for point on right edge of dtm */
	if (ll_col == Header.columns - 1l) {
		if (x > (Header.origin_x + (Header.columns - 1l) * Header.column_spacing)) {
			/* do a second test */
			if (x > (Header.origin_x + (Header.columns - 1l) * Header.column_spacing + 0.001)) {
				return(-32767.0);
			}
		}
	}

	/* check for point on top edge of dtm */
	if (ll_pt == Header.points - 1l) {
		if (y > (Header.origin_y + (Header.points - 1l) * Header.point_spacing)) {
			/* do a second test */
			if (y > (Header.origin_y + (Header.points - 1l) * Header.point_spacing + 0.001)) {
				return(-32767.0);
			}
		}
	}

	/* compute the local patch coordinates */
	cell_llx = ll_col - patch_llx;
	cell_lly = ll_pt - patch_lly;

	llz = ReadInternalPatchElevationValue(cell_llx, cell_lly);
//	llz = (double) lpsDTMPatch[cell_llx] [cell_lly];

	/* check to see that point is not on right edge of dtm */
	if (ll_col == Header.columns - 1l)
		lrz = llz;
	else
		lrz = ReadInternalPatchElevationValue(cell_llx + 1, cell_lly);
//		lrz = (double) lpsDTMPatch[cell_llx + 1l] [cell_lly];

	/* check to see that point is not on top edge of dtm */
	if (ll_pt == Header.points - 1l)
		ulz = llz;
	else
		ulz = ReadInternalPatchElevationValue(cell_llx, cell_lly + 1);
//		ulz = (double) lpsDTMPatch[cell_llx] [cell_lly + 1l];

	/* check to see that point is not on upper right corner of dtm */
	if ((ll_col == Header.columns - 1l) && (ll_pt == Header.points - 1l))
		urz = llz;
	else {
		if (ll_col == Header.columns - 1)
			urz = ulz;
		else if (ll_pt == Header.points - 1)
			urz = lrz;
		else
			urz = ReadInternalPatchElevationValue(cell_llx + 1, cell_lly + 1);
//			urz = (double) lpsDTMPatch[cell_llx + 1l] [cell_lly + 1l];
	}

	// calculate normalized coordinate...relative to lower left of cell
	// cell is 1 unit on a side regardless of actual dimensions
	nx = modf((x - (Header.origin_x)) / Header.column_spacing, &dummy);
	ny = modf((y - (Header.origin_y)) / Header.point_spacing, &dummy);

	// check to see if point is on a cell corner
	if (nx == 0.0 && ny == 0.0) {
		return(llz);
	}
	if (nx == 1.0 && ny == 0.0) {
		return(lrz);
	}
	if (nx == 1.0 && ny == 1.0) {
		return(urz);
	}
	if (nx == 0.0 && ny == 1.0) {
		return(ulz);
	}

	// set elevation to -1 then look for maximum value from bounding cell
	elev = -1.0;

	elev = max(elev, llz);
	elev = max(elev, lrz);
	elev = max(elev, ulz);
	elev = max(elev, urz);

	if (elev >= 0.0)
		elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;

	return(elev);
}

BOOL PlansDTM::CreateDummyModel(CString FileName, double OriginX, double OriginY, int Cols, int Rows, double ColSpacing, double RowSpacing, int XYunits, int Zunits, int Elevation)
{
	BIN_HEADER dtm;

	dtm.columns = Cols;
	dtm.points = Rows;
	dtm.column_spacing = ColSpacing;
	dtm.point_spacing = RowSpacing;
	dtm.origin_x = OriginX;
	dtm.origin_y = OriginY;
	dtm.rotation = 0.0;
	dtm.version = 1.0;
	dtm.xy_units = XYunits;
	dtm.z_units = Zunits;
	dtm.z_bytes = 0;
	dtm.max_z = dtm.min_z = (double) Elevation;
	strcpy(dtm.name, "Dummy model");
	strcpy(dtm.signature, "PLANS-PC BINARY .DTM");

	FILE* f;
	f = fopen((LPCTSTR) FileName, "wb");
	if (f) {
		// model is open
		fwrite(&dtm.signature, sizeof(char), 21, f);
		fwrite(&dtm.name, sizeof(char), 61, f);
		fwrite(&dtm.version, sizeof(float), 1, f);
		fwrite(&dtm.origin_x, sizeof(double), 1, f);
		fwrite(&dtm.origin_y, sizeof(double), 1, f);
		fwrite(&dtm.min_z, sizeof(double), 1, f);
		fwrite(&dtm.max_z, sizeof(double), 1, f);
		fwrite(&dtm.rotation, sizeof(double), 1, f);
		fwrite(&dtm.column_spacing, sizeof(double), 1, f);
		fwrite(&dtm.point_spacing, sizeof(double), 1, f);
		fwrite(&dtm.columns, sizeof(long), 1, f);
		fwrite(&dtm.points, sizeof(long), 1, f);
		fwrite(&dtm.xy_units, sizeof(short), 1, f);
		fwrite(&dtm.z_units, sizeof(short), 1, f);
		fwrite(&dtm.z_bytes, sizeof(short), 1, f);

		fseek(f, 200l, SEEK_SET);

		for (int i = 0; i < Rows * Cols; i ++)
			fwrite(&Elevation, sizeof(short), 1, f);

		fclose (f);

		return(TRUE);
	}
	return(FALSE);
}

int PlansDTM::GetXYUnits()
{
	if (Valid)
		return((int) Header.xy_units);
	else
		return(-1);
}

void PlansDTM::GetDescriptiveName(CString& csName)
{
	if (Valid)
		csName = _T(Header.name);
	else
		csName.Empty();
}

void PlansDTM::GetDescriptiveName(LPSTR lpszName)
{
	if (Valid)
		strcpy(lpszName, Header.name);
	else
		lpszName[0] = '\0';
}

int PlansDTM::GetElevationUnits()
{
	if (Valid)
		return((int) Header.z_units);
	else
		return(-1);
}

int PlansDTM::GetZBytes()
{
	if (Valid)
		return((int) Header.z_bytes);
	else
		return(-1);
}

double PlansDTM::ReadColumnValue(void *col, int row)
{
	double elevation;
	switch (Header.z_bytes) {
		case 0:		// short
		default:
			elevation = (double) ((short*) col)[row];
			break;
		case 1:		// int
			elevation = (double) ((int*) col)[row];
			break;
		case 2:		// float
			elevation = (double) ((float*) col)[row];
			break;
		case 3:		// double
			elevation = ((double*) col)[row];
			break;
	}
	return(elevation);
}

double PlansDTM::ReadInternalElevationValue(int col, int row, BOOL FixVoid)
{
	double elevation = -1.0;
/*	if (Header.z_bytes == 0)
		elevation = (double) ((short**) lpsElevData)[col] [row];
	else if (Header.z_bytes == 1)
		elevation = (double) ((int**) lpsElevData)[col] [row];
	else if (Header.z_bytes == 2)
		elevation = (double) ((float**) lpsElevData)[col] [row];
	else
		elevation = ((double**) lpsElevData)[col] [row];
*/
	if (col >= 0 && col < Header.columns && row >= 0 && row < Header.points) {
		switch (Header.z_bytes) {
			case 0:		// short
			default:
				elevation = (double) ((short**) lpsElevData)[col] [row];
				break;
			case 1:		// int
				elevation = (double) ((int**) lpsElevData)[col] [row];
				break;
			case 2:		// float
				elevation = (double) ((float**) lpsElevData)[col] [row];
				break;
			case 3:		// double
				elevation = ((double**) lpsElevData)[col] [row];
				break;
		}

		if (FixVoid && elevation < 0.0)
			elevation = Header.min_z;
	}

	return(elevation);
}

double PlansDTM::ReadInternalPatchElevationValue(int col, int row, BOOL FixVoid)
{
	double elevation;
	switch (Header.z_bytes) {
		case 0:		// short
		default:
			elevation = (double) ((short**) lpsDTMPatch)[col] [row];
			break;
		case 1:		// int
			elevation = (double) ((int**) lpsDTMPatch)[col] [row];
			break;
		case 2:		// float
			elevation = (double) ((float**) lpsDTMPatch)[col] [row];
			break;
		case 3:		// double
			elevation = ((double**) lpsDTMPatch)[col] [row];
			break;
	}
	
	if (FixVoid && elevation < 0.0)
		elevation = Header.min_z;

		return(elevation);
}

void PlansDTM::SetInternalElevationValue(int col, int row, double value)
{
	if (col >= 0 && col < Header.columns && row >= 0 && row < Header.points) {
		switch (Header.z_bytes) {
			case 0:		// short
			default:
				((short**) lpsElevData)[col] [row] = (short) value;
				break;
			case 1:		// int
				((int**) lpsElevData)[col] [row] = (int) value;
				break;
			case 2:		// float
				((float**) lpsElevData)[col] [row] = (float) value;
				break;
			case 3:		// double
				((double**) lpsElevData)[col] [row] = value;
				break;
		}
	}
}

void PlansDTM::SetInternalPatchElevationValue(int col, int row, double value)
{
	if (col >= 0 && col < Header.columns && row >= 0 && row < Header.points) {
		switch (Header.z_bytes) {
			case 0:		// short
			default:
				((short**) lpsDTMPatch)[col] [row] = (short) value;
				break;
			case 1:		// int
				((int**) lpsDTMPatch)[col] [row] = (int) value;
				break;
			case 2:		// float
				((float**) lpsDTMPatch)[col] [row] = (float) value;
				break;
			case 3:		// double
				((double**) lpsDTMPatch)[col] [row] = value;
				break;
		}
	}
}

double PlansDTM::GetGridElevation(int col, int row)
{
	if (Valid) {
		if (HaveElevations) {
			if (col >= 0 && col < Header.columns && row >= 0 && row < Header.points)
				return(ReadInternalElevationValue(col, row));
//				return((double) lpsElevData[col][row]);
		}
		else if (UsingPatchElev) {
			double x = Header.origin_x + (double) col * Header.column_spacing;
			double y = Header.origin_y + (double) row * Header.point_spacing;
			return(InterpolateElev(x, y, FALSE));
		}
	}
	return(-1.0);
}

float PlansDTM::Version()
{
	if (Valid)
		return(Header.version);
	else
		return(0.0f);
}

int PlansDTM::CoordinateSystem()
{
	if (Valid && Header.coord_sys > 0)
		return((int) Header.coord_sys);
	else
		return(0);
}

int PlansDTM::CoordinateZone()
{
	if (Valid && Header.coord_zone > 0)
		return((int) Header.coord_zone);
	else
		return(0);
}

int PlansDTM::HorizontalDatum()
{
	if (Valid && Header.horizontal_datum > 0)
		return((int) Header.horizontal_datum);
	else
		return(0);
}

int PlansDTM::VerticalDatum()
{
	if (Valid && Header.vertical_datum > 0)
		return((int) Header.vertical_datum);
	else
		return(0);
}

void PlansDTM::SetVerticalExaggeration(double ve)
{
	VerticalExaggeration = ve;
}

BOOL PlansDTM::WriteASCIIXYZ(LPCTSTR FileName, double XYfactor, double Zfactor, BOOL OutputVoidAreas)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	FILE* f = fopen(FileName, "wt");
	if (f) {
		int i, j;
		double px, py, pz;
		for (i = 0; i < Header.columns; i ++) {
			px = Header.origin_x + ((double) i * Header.column_spacing);
			py = Header.origin_y;

			for (j = 0; j < Header.points; j ++) {
				pz = ReadInternalElevationValue(i, j);
				if (OutputVoidAreas || (pz >= 0.0 && !OutputVoidAreas)) {
					if (pz < 0.0)
						fprintf(f, "%.4lf %.4lf %.4lf\n", px * XYfactor, py * XYfactor, -9999.0);
					else
						fprintf(f, "%.4lf %.4lf %.4lf\n", px * XYfactor, py * XYfactor, pz * Zfactor);
				}
				py += Header.point_spacing;
			}
		}

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

BOOL PlansDTM::WriteASCIIXYZCSV(LPCTSTR FileName, double XYfactor, double Zfactor, BOOL OutputVoidAreas, BOOL OutputHeader)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	FILE* f = fopen(FileName, "wt");
	if (f) {
		int i, j;
		double px, py, pz;
		// print header
		if (OutputHeader)
			fprintf(f, "X,Y,Elevation\n");
		for (i = 0; i < Header.columns; i ++) {
			px = Header.origin_x + ((double) i * Header.column_spacing);
			py = Header.origin_y;

			for (j = 0; j < Header.points; j ++) {
				pz = ReadInternalElevationValue(i, j);
				if (OutputVoidAreas || (pz >= 0.0 && !OutputVoidAreas)) {
					if (pz < 0.0)
						fprintf(f, "%.4lf,%.4lf,%.4lf\n", px * XYfactor, py * XYfactor, -9999.0);
					else
						fprintf(f, "%.4lf,%.4lf,%.4lf\n", px * XYfactor, py * XYfactor, pz * Zfactor);
				}
				py += Header.point_spacing;
			}
		}

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

BOOL PlansDTM::WriteTriangleFile(LPCTSTR FileName, double BaseHeight, BOOL MakeEPOB)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	BOOL UseColor = FALSE;

	FILE* f = fopen(FileName, "wt");
	if (MakeEPOB)
		fprintf(f, "EPOB\n1.0\nTerrain model\n");

	if (f) {
		int i, j;
		double px, py, pz1, pz2, pz3;
		for (i = 0; i < Header.columns - 1; i ++) {
//			px = Header.origin_x + ((double) i * Header.column_spacing);
//			py = Header.origin_y;
			px = ((double) i * Header.column_spacing);
			py = 0.0;
			if (i == 0) {
				// left side wall
				if (MakeEPOB && UseColor)
					fprintf(f, "setcolor 255 0 0\n");

				for (j = 0; j < Header.points - 1; j ++) {
					pz1 = ReadInternalElevationValue(i, j);
					pz2 = ReadInternalElevationValue(i, j + 1);

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py + Header.point_spacing, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf", px, py, pz1);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py, pz1);
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py + Header.point_spacing, pz2);
					fprintf(f, "%.4lf %.4lf %.4lf", px, py + Header.point_spacing, Header.min_z - BaseHeight);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					py += Header.point_spacing;
				}
				if (MakeEPOB && UseColor)
					fprintf(f, "setcolor 192 192 192\n");
			}
			else if (i == Header.columns - 2) {
				// right side wall
				if (MakeEPOB && UseColor)
					fprintf(f, "setcolor 0 255 0\n");

				for (j = 0; j < Header.points - 1; j ++) {
					pz1 = ReadInternalElevationValue(i + 1, j);
					pz2 = ReadInternalElevationValue(i + 1, j + 1);
				
					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py, pz1);
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf", px + Header.column_spacing, py + Header.point_spacing, Header.min_z - BaseHeight);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py + Header.point_spacing, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py + Header.point_spacing, pz2);
					fprintf(f, "%.4lf %.4lf %.4lf", px + Header.column_spacing, py, pz1);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					py += Header.point_spacing;
				}
				if (MakeEPOB && UseColor)
					fprintf(f, "setcolor 192 192 192\n");
			}

//			py = Header.origin_y;
			py = 0.0;
			for (j = 0; j < Header.points - 1; j ++) {
				pz1 = ReadInternalElevationValue(i, j);
				pz2 = ReadInternalElevationValue(i + 1, j);
				pz3 = ReadInternalElevationValue(i + 1, j + 1);
				if (j == 0) {
					// bottom wall
					if (MakeEPOB && UseColor)
						fprintf(f, "setcolor 0 0 255\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf", px + Header.column_spacing, py, pz2);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py, pz2);
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py, pz1);
					fprintf(f, "%.4lf %.4lf %.4lf", px, py, Header.min_z - BaseHeight);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");
		
					if (MakeEPOB && UseColor)
						fprintf(f, "setcolor 192 192 192\n");
				}

				// bottom right triangle
				if (MakeEPOB)
					fprintf(f, "triangle ");
				fprintf(f, "%.4lf %.4lf %.4lf ", px, py, pz1);
				fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py, pz2);
				fprintf(f, "%.4lf %.4lf %.4lf", px + Header.column_spacing, py + Header.point_spacing, pz3);
				if (MakeEPOB)
					fprintf(f, " 0\n");
				else
					fprintf(f, "\n");

				// top left triangle
				pz2 = ReadInternalElevationValue(i, j + 1);
				if (MakeEPOB)
					fprintf(f, "triangle ");
				fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py + Header.point_spacing, pz3);
				fprintf(f, "%.4lf %.4lf %.4lf ", px, py + Header.point_spacing, pz2);
				fprintf(f, "%.4lf %.4lf %.4lf", px, py, pz1);
				if (MakeEPOB)
					fprintf(f, " 0\n");
				else
					fprintf(f, "\n");

				if (j == Header.points - 2) {
					// top wall
					if (MakeEPOB && UseColor)
						fprintf(f, "setcolor 255 255 0\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py + Header.point_spacing, pz2);
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py + Header.point_spacing, pz3);
					fprintf(f, "%.4lf %.4lf %.4lf", px + Header.column_spacing, py + Header.point_spacing, Header.min_z - BaseHeight);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					if (MakeEPOB)
						fprintf(f, "triangle ");
					fprintf(f, "%.4lf %.4lf %.4lf ", px + Header.column_spacing, py + Header.point_spacing, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf ", px, py + Header.point_spacing, Header.min_z - BaseHeight);
					fprintf(f, "%.4lf %.4lf %.4lf", px, py + Header.point_spacing, pz2);
					if (MakeEPOB)
						fprintf(f, " 0\n");
					else
						fprintf(f, "\n");

					if (MakeEPOB && UseColor)
						fprintf(f, "setcolor 192 192 192\n");
				}

				py += Header.point_spacing;
			}
		}

		// add bottom to model area
		if (MakeEPOB && UseColor)
			fprintf(f, "setcolor 0 255 255\n");

		if (MakeEPOB)
			fprintf(f, "triangle ");
//		fprintf(f, "%.4lf %.4lf %.4lf ", Header.origin_x, Header.origin_y, Header.min_z - BaseHeight);
//		fprintf(f, "%.4lf %.4lf %.4lf ", Header.origin_x, Header.origin_y + Height(), Header.min_z - BaseHeight);
//		fprintf(f, "%.4lf %.4lf %.4lf", Header.origin_x + Width(), Header.origin_y + Height(), Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf ", 0.0, 0.0, Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf ", 0.0, Height(), Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf", Width(), Height(), Header.min_z - BaseHeight);
		if (MakeEPOB)
			fprintf(f, " 0\n");
		else
			fprintf(f, "\n");

		if (MakeEPOB)
			fprintf(f, "triangle ");
//		fprintf(f, "%.4lf %.4lf %.4lf ", Header.origin_x + Width(), Header.origin_y + Height(), Header.min_z - BaseHeight);
//		fprintf(f, "%.4lf %.4lf %.4lf ", Header.origin_x + Width(), Header.origin_y, Header.min_z - BaseHeight);
//		fprintf(f, "%.4lf %.4lf %.4lf", Header.origin_x, Header.origin_y, Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf ", Width(), Height(), Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf ", Width(), 0.0, Header.min_z - BaseHeight);
		fprintf(f, "%.4lf %.4lf %.4lf", 0.0, 0.0, Header.min_z - BaseHeight);
		if (MakeEPOB)
			fprintf(f, " 0\n");
		else
			fprintf(f, "\n");

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

BOOL PlansDTM::WriteASCIIGrid(LPCTSTR FileName, double Zfactor, BOOL DataIsRaster)
{
	if (!Valid)
		return(FALSE);

//	if (!HaveElevations && !UsingPatchElev)
//		return(FALSE);

	FILE* f = fopen(FileName, "wt");
	if (f) {
		// print header
		fprintf(f, "ncols %i\n", Header.columns);
		fprintf(f, "nrows %i\n", Header.points);
		if (DataIsRaster) {
			fprintf(f, "xllcenter %.4lf\n", Header.origin_x);
			fprintf(f, "yllcenter %.4lf\n", Header.origin_y);
		}
		else {
			fprintf(f, "xllcorner %.4lf\n", Header.origin_x);
			fprintf(f, "yllcorner %.4lf\n", Header.origin_y);
		}
		fprintf(f, "cellsize %.4lf\n", Header.column_spacing);
		if (Header.z_bytes == 0 || Header.z_bytes == 1)
			fprintf(f, "nodata_value -9999\n");
		else
			fprintf(f, "nodata_value -9999\n");

		int i, j;
		double pz;
		if (HaveElevations) {
			for (j = Header.points - 1; j >= 0; j --) {
				for (i = 0; i < Header.columns; i ++) {
					pz = ReadInternalElevationValue(i, j);
					if (pz < 0.0) {
						fprintf(f, "%.0lf ", -9999.0);
					}
					else {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, "%.0lf ", pz * Zfactor);
						else
							fprintf(f, "%.6lf ", pz * Zfactor);
					}
				}
				fprintf(f, "\n");
			}
		}
		else {
			// using point access logic
			// open model file
			OpenModelFile();
			for (j = Header.points - 1; j >= 0; j --) {
				for (i = 0; i < Header.columns; i ++) {
					pz = LoadDTMPoint(i, j);
					if (pz < 0.0) {
						fprintf(f, "%.0lf ", -9999.0);
					}
					else {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, "%.0lf ", pz * Zfactor);
						else
							fprintf(f, "%.6lf ", pz * Zfactor);
					}
				}
				fprintf(f, "\n");
			}
			// close model file
			CloseModelFile();
		}

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

BOOL PlansDTM::WriteSurferASCIIGrid(LPCTSTR FileName, double Zfactor)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	FILE* f = fopen(FileName, "wt");
	if (f) {
		// print header
		fprintf(f, "DSAA\n");
		fprintf(f, "%i %i\n", Header.columns, Header.points);
		fprintf(f, "%.4lf %.4lf\n", Header.origin_x, Header.origin_x + (double) Header.columns * Header.column_spacing);
		fprintf(f, "%.4lf %.4lf\n", Header.origin_y, Header.origin_y + (double) Header.points * Header.point_spacing);
		fprintf(f, "%.4lf %.4lf\n", Header.min_z, Header.max_z);

		int i, j;
		double pz;
		for (j = 0; j < Header.points; j ++) {
			for (i = 0; i < Header.columns; i ++) {
				pz = ReadInternalElevationValue(i, j);
				if (pz < 0.0) {
					if (Header.z_bytes == 0 || Header.z_bytes == 1)
						fprintf(f, "%.0lf ", -32767.0);
					else
						fprintf(f, "%.4lf ", -32767.0);
				}
				else {
					if (Header.z_bytes == 0 || Header.z_bytes == 1)
						fprintf(f, "%.0lf ", pz * Zfactor);
					else
						fprintf(f, "%.4lf ", pz * Zfactor);
				}
			}
			fprintf(f, "\n");
		}

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

double PlansDTM::GetElevationConversion()
{
	return(ElevationConversion);
}

void PlansDTM::MovingWindowFilter(int FilterSize, double *Weights, BOOL PreserveLocalPeaks, int PeakRule)
{
	if (!Valid || !HaveElevations)
		return;

	int i, j, k, l;
	double weightsum;
	double* e;
	int nindex;		// neighbor index
	double valuesum;
	double centerelev;
	double newelev;
	int FilterHalfSize = FilterSize / 2;
	BOOL cflag;
	
	// filter window must be odd
	if (FilterSize % 2 == 0)
		return;

	// get memory for neighbor values
	e = new double [FilterSize * FilterSize];

	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points)) {
		delete [] e;
		return;
	}

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// get center point
			if (PreserveLocalPeaks)
				centerelev = ReadInternalElevationValue(i, j);

			// get neighbor pixels
			nindex = 0;
			valuesum = 0.0;
			weightsum = 0.0;
			for (k = -FilterHalfSize; k <= FilterHalfSize; k ++) {
				for (l = -FilterHalfSize; l <= FilterHalfSize; l ++) {
					e[nindex] = ReadInternalElevationValue(i - k, j - l);
					if (e[nindex] >= 0.0) {
						valuesum += Weights[nindex] * e[nindex];
						weightsum += Weights[nindex];
					}
					nindex ++;
				}
			}

			if (nindex) {
				// see if center point is a peak...if it is a peak, don't apply filter
				cflag = FALSE;
				if (!PreserveLocalPeaks)
					cflag = TRUE;
				else {
					for (k = 0; k < nindex; k ++) {
						if (PeakRule == HIGHPEAKS) {
							if (e[k] > centerelev)
								cflag = TRUE;
						}
						else {
							if (e[k] < centerelev)
								cflag = TRUE;
						}
					}
				}

				if (weightsum > 0.0 && cflag) {
					newelev = valuesum / weightsum;

					// assign new elevation
					if (Header.z_bytes == 0) 		// short
						((short**) TempElev)[i][j] = (short) newelev;
					else if (Header.z_bytes == 1) 		// 4-byte int
						((int**) TempElev)[i][j] = (int) newelev;
					else if (Header.z_bytes == 2) 		// float
						((float**) TempElev)[i][j] = (float) newelev;
					else if (Header.z_bytes == 3) 		// double
						((double**) TempElev)[i][j] = newelev;
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	delete [] e;
}

void PlansDTM::MedianFilter(int FilterSize, BOOL PreserveLocalPeaks, int PeakRule)
{
	if (!Valid || !HaveElevations)
		return;

	int i, j, k, l;
	int ncount;		// neighbor count
	double* values;
	double elev;
	double centerelev;
	double newelev;
	int FilterHalfSize = FilterSize / 2;
	BOOL cflag;
	double slopetest = Header.point_spacing * 2.5;

	// filter window must be odd
	if (FilterSize % 2 == 0)
		return;

	// get memory for neighbor values
	values = new double [FilterSize * FilterSize];

	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points)) {
		delete [] values;
		return;
	}

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// get center point
			if (PreserveLocalPeaks)
				centerelev = ReadInternalElevationValue(i, j);

			// get neighbor pixels
			ncount = 0;
			for (k = -FilterHalfSize; k <= FilterHalfSize; k ++) {
				for (l = -FilterHalfSize; l <= FilterHalfSize; l ++) {
					elev = ReadInternalElevationValue(i - k, j - l);
					if (elev >= 0.0) {
						values[ncount] = elev;
						ncount ++;
					}
				}
			}

			if (ncount) {
				// see if center point is a peak...if it is a peak, don't apply filter
				cflag = FALSE;
				if (!PreserveLocalPeaks)
					cflag = TRUE;
				else {
					for (k = 0; k < ncount; k ++) {
						if (PeakRule == HIGHPEAKS) {
							if (values[k] > centerelev)
								cflag = TRUE;
						}
						else {
							if (values[k] < centerelev)
								cflag = TRUE;
						}
					}
				}

				if (cflag) {
					// sort list of neighbors
					qsort(values, ncount, sizeof(double), compelev);

					// if odd number of neighbors...assign middle value
					if (ncount % 2)
						newelev = values[ncount / 2];
					else
						newelev = ((double) (values[ncount / 2] + values[ncount / 2 - 1]) / 2.0);

					// assign new elevation
					if (Header.z_bytes == 0) 		// short
						((short**) TempElev)[i][j] = (short) newelev;
					else if (Header.z_bytes == 1) 		// 4-byte int
						((int**) TempElev)[i][j] = (int) newelev;
					else if (Header.z_bytes == 2) 		// float
						((float**) TempElev)[i][j] = (float) newelev;
					else if (Header.z_bytes == 3) 		// double
						((double**) TempElev)[i][j] = newelev;
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	// free values array
	delete [] values;
}

BOOL PlansDTM::WriteModel(LPCTSTR FileName)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	// open file and write header
	FILE* f;
	f = fopen((LPCTSTR) FileName, "wb");
	if (f) {
		// scan for min/max elevation
		Header.min_z = 1000000.0;
		Header.max_z = -1000000.0;
		double elevation = 0.0;
		for (int i = 0; i < Header.columns; i ++) {
			for (int j = 0; j < Header.points; j ++) {
				if (Header.z_bytes == 0) 		// short
					elevation = (double) ((short**) lpsElevData)[i][j];
				else if (Header.z_bytes == 1) 		// 4-byte int
					elevation = (double) ((int**) lpsElevData)[i][j];
				else if (Header.z_bytes == 2) 		// float
					elevation = (double) ((float**) lpsElevData)[i][j];
				else if (Header.z_bytes == 3) 		// double
					elevation = (double) ((double**) lpsElevData)[i][j];

				if (elevation >= 0.0) {
					if (elevation > Header.max_z)
						Header.max_z = elevation;
					else if (elevation < Header.min_z)
						Header.min_z = elevation;
				}
			}
		}

		// model is open
		fwrite(&Header.signature, sizeof(char), 21, f);
		fwrite(&Header.name, sizeof(char), 61, f);
		fwrite(&Header.version, sizeof(float), 1, f);
		fwrite(&Header.origin_x, sizeof(double), 1, f);
		fwrite(&Header.origin_y, sizeof(double), 1, f);
		fwrite(&Header.min_z, sizeof(double), 1, f);
		fwrite(&Header.max_z, sizeof(double), 1, f);
		fwrite(&Header.rotation, sizeof(double), 1, f);
		fwrite(&Header.column_spacing, sizeof(double), 1, f);
		fwrite(&Header.point_spacing, sizeof(double), 1, f);
		fwrite(&Header.columns, sizeof(long), 1, f);
		fwrite(&Header.points, sizeof(long), 1, f);
		fwrite(&Header.xy_units, sizeof(short), 1, f);
		fwrite(&Header.z_units, sizeof(short), 1, f);
		fwrite(&Header.z_bytes, sizeof(short), 1, f);
		fwrite(&Header.coord_sys, sizeof(short), 1, f);
		fwrite(&Header.coord_zone, sizeof(short), 1, f);
		fwrite(&Header.horizontal_datum, sizeof(short), 1, f);
		fwrite(&Header.vertical_datum, sizeof(short), 1, f);

		// jump to start of data
		fseek(f, 200l, SEEK_SET);

		// write elevations...depends on storage format for elevations
		for (int i = 0; i < Header.columns; i ++) {
			if (Header.z_bytes == 0) {		// short
				fwrite(lpsElevData[i], sizeof(short), (size_t) Header.points, f);
			}
			else if (Header.z_bytes == 1) {		// 4-byte int
				fwrite(lpsElevData[i], sizeof(int), (size_t) Header.points, f);
			}
			else if (Header.z_bytes == 2) {		// float
				fwrite(lpsElevData[i], sizeof(float), (size_t) Header.points, f);
			}
			else if (Header.z_bytes == 3) {		// double
				fwrite(lpsElevData[i], sizeof(double), (size_t) Header.points, f);
			}
		}

		fclose (f);

		return(TRUE);
	}
	return(FALSE);
}

void PlansDTM::FillHoles(int PeakRule, int MaxCells, double HoleThreshold)
{
	if (!Valid || !HaveElevations)
		return;

	int i, j, k;
	double elev;
	double elevsum;
	double weightsum;
	double newelev;
	double values[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	double weights[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	int ncount;
	unsigned char vector;
	int maxcells = MaxCells;

	// don't want to fill holes along edges of model...
	// create arrays to hold the first and last good data point in each row and column
	// only do the filling for these points

	int** RowStartStop = AllocateIntArray(Header.points, 2);
	int** ColStartStop = AllocateIntArray(Header.columns, 2);

	// initialize row and column arrays to -1
	for (i = 0; i < Header.columns; i ++) {
		ColStartStop[i][0] = -1;
		ColStartStop[i][1] = -1;
	}
	for (j = 0; j < Header.points; j ++) {
		RowStartStop[j][0] = -1;
		RowStartStop[j][1] = -1;
	}

	// scan current model to find first and last valid points in each row and column
	// scan for start of data in rows
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (ReadInternalElevationValue(i, j) >= HoleThreshold) {
				ColStartStop[i][0] = j;
				break;
			}
		}
	}
	// scan for end of data in rows
	for (i = 0; i < Header.columns; i ++) {
		for (j = Header.points - 1; j >= 0 ; j --) {
			if (ReadInternalElevationValue(i, j) >= HoleThreshold) {
				ColStartStop[i][1] = j;
				break;
			}
		}
	}

	// scan for start of data in columns
	for (j = 0; j < Header.points; j ++) {
		for (i = 0; i < Header.columns; i ++) {
			if (ReadInternalElevationValue(i, j) >= HoleThreshold) {
				RowStartStop[j][0] = i;
				break;
			}
		}
	}
	// scan for end of data in columns
	for (j = 0; j < Header.points; j ++) {
		for (i = Header.columns - 1; i >= 0; i --) {
			if (ReadInternalElevationValue(i, j) >= HoleThreshold) {
				RowStartStop[j][1] = i;
				break;
			}
		}
	}

	// allocate memory for temp model...type matches dtm elevation type
	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points))
		return;

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	// scan the columns
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// use RowStartStop and ColStartStop arrays to control which points are "filled"
			if (j < ColStartStop[i][0] || j > ColStartStop[i][1])
				continue;
			if (ColStartStop[i][0] == -1 && ColStartStop[i][1] == -1)
				continue;
			
			// get center point
			if (ReadInternalElevationValue(i, j) < HoleThreshold) {
				// look for valid neighbors
				ncount = 0;
				vector = 0;

				// look left
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 1;
						break;
					}
				}

				// look right
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 16;
						break;
					}
				}

				// look up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 64;
						break;
					}
				}

				// look down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 4;
						break;
					}
				}

				// look left and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 128;
						break;
					}
				}

				// look right and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 32;
						break;
					}
				}

				// look left and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 2;
						break;
					}
				}

				// look right and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 8;
						break;
					}
				}

				// we have to have valid cells on opposite sides of the target point to do interpolation
				if ((vector & 68) == 68 || (vector & 17) == 17 || (vector & 34) == 34 || (vector & 136) == 136) {
					if (ncount >= 4) {
						if (PeakRule == -1) {
							// average
							elevsum = 0.0;
							weightsum = 0.0;
							for (k = 0; k < ncount; k ++) {
								elevsum += values[k] * weights[k];
								weightsum += weights[k];
							}

							newelev = elevsum / weightsum;
						}
						else if (PeakRule == HIGHPEAKS) {
							// keep max value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = max(elevsum, values[k]);

							newelev = elevsum;
						}
						else {
							// keep min value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = min(elevsum, values[k]);

							newelev = elevsum;
						}
					}
					else if (ncount > 0) {
						// no peak preservation...just average the values
						elevsum = 0.0;
						weightsum = 0.0;
						for (k = 0; k < ncount; k ++) {
							elevsum += (values[k] * weights[k]);
							weightsum += weights[k];
						}

						newelev = elevsum / weightsum;
					}

					// assign new elevation
					if (ncount > 0) {
						if (Header.z_bytes == 0) 		// short
							((short**) TempElev)[i][j] = (short) newelev;
						else if (Header.z_bytes == 1) 		// 4-byte int
							((int**) TempElev)[i][j] = (int) newelev;
						else if (Header.z_bytes == 2) 		// float
							((float**) TempElev)[i][j] = (float) newelev;
						else if (Header.z_bytes == 3) 		// double
							((double**) TempElev)[i][j] = newelev;
					}
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// scan the rows
	for (j = 0; j < Header.points; j ++) {
		for (i = 0; i < Header.columns; i ++) {
			// use RowStartStop and ColStartStop arrays to control which points are "filled"
			if (i < RowStartStop[j][0] || i > RowStartStop[j][1])
				continue;
			if (RowStartStop[j][0] == -1 && RowStartStop[j][1] == -1)
				continue;
			
			// get center point
			if (ReadInternalElevationValue(i, j) < HoleThreshold) {
				// look for valid neighbors
				ncount = 0;
				vector = 0;

				// look left
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 1;
						break;
					}
				}

				// look right
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 16;
						break;
					}
				}

				// look up 
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 64;
						break;
					}
				}

				// look down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 4;
						break;
					}
				}

				// look left and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 128;
						break;
					}
				}

				// look right and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j + k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 32;
						break;
					}
				}

				// look left and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 2;
						break;
					}
				}

				// look right and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j - k);
					if (elev > HoleThreshold) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 8;
						break;
					}
				}

				// we have to have valid cells on opposite sides of the target point to do interpolation
				if ((vector & 68) == 68 || (vector & 17) == 17 || (vector & 34) == 34 || (vector & 136) == 136) {
					if (ncount >= 4) {
						if (PeakRule == -1) {
							// average
							elevsum = 0.0;
							weightsum = 0.0;
							for (k = 0; k < ncount; k ++) {
								elevsum += (values[k] * weights[k]);
								weightsum += weights[k];
							}

							newelev = elevsum / weightsum;
						}
						else if (PeakRule == HIGHPEAKS) {
							// keep max value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = max(elevsum, values[k]);

							newelev = elevsum;
						}
						else {
							// keep min value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = min(elevsum, values[k]);

							newelev = elevsum;
						}
					}
					else if (ncount > 0) {
						// no peak preservation...just average the values
						elevsum = 0.0;
						weightsum = 0.0;
						for (k = 0; k < ncount; k ++) {
							elevsum += values[k] * weights[k];
							weightsum += weights[k];
						}

						newelev = elevsum / weightsum;
					}

					// assign new elevation
					if (ncount > 0) {
						if (Header.z_bytes == 0) 		// short
							((short**) TempElev)[i][j] = (short) newelev;
						else if (Header.z_bytes == 1) 		// 4-byte int
							((int**) TempElev)[i][j] = (int) newelev;
						else if (Header.z_bytes == 2) 		// float
							((float**) TempElev)[i][j] = (float) newelev;
						else if (Header.z_bytes == 3) 		// double
							((double**) TempElev)[i][j] = newelev;
					}
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	DeallocateIntArray(RowStartStop, Header.points);
	DeallocateIntArray(ColStartStop, Header.columns);
}

void PlansDTM::OriginalFillHoles(int PeakRule, int MaxCells)
{
	if (!Valid || !HaveElevations)
		return;

	int i, j, k;
	double elev;
	double elevsum;
	double weightsum;
	double newelev;
	double values[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	double weights[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	int ncount;
	unsigned char vector;
	int maxcells = MaxCells;

	// don't want to fill holes along edges of model...
	// create arrays to hold the first and last good data point in each row and column
	// only do the filling for these points

	int** RowStartStop = AllocateIntArray(Header.points, 2);
	int** ColStartStop = AllocateIntArray(Header.columns, 2);

	// initialize row and column arrays to -1
	for (i = 0; i < Header.columns; i ++) {
		ColStartStop[i][0] = -1;
		ColStartStop[i][1] = -1;
	}
	for (j = 0; j < Header.points; j ++) {
		RowStartStop[j][0] = -1;
		RowStartStop[j][1] = -1;
	}

	// scan current model to find first and last valid points in each row and column
	// scan for start of data in rows
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (ReadInternalElevationValue(i, j) >= 0.0) {
				ColStartStop[i][0] = j;
				break;
			}
		}
	}
	// scan for end of data in rows
	for (i = 0; i < Header.columns; i ++) {
		for (j = Header.points - 1; j >= 0 ; j --) {
			if (ReadInternalElevationValue(i, j) >= 0.0) {
				ColStartStop[i][1] = j;
				break;
			}
		}
	}

	// scan for start of data in columns
	for (j = 0; j < Header.points; j ++) {
		for (i = 0; i < Header.columns; i ++) {
			if (ReadInternalElevationValue(i, j) >= 0.0) {
				RowStartStop[j][0] = i;
				break;
			}
		}
	}
	// scan for end of data in columns
	for (j = 0; j < Header.points; j ++) {
		for (i = Header.columns - 1; i >= 0; i --) {
			if (ReadInternalElevationValue(i, j) >= 0.0) {
				RowStartStop[j][1] = i;
				break;
			}
		}
	}

	// allocate memory for temp model...type matches dtm elevation type
	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points))
		return;

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	// scan the columns
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// use RowStartStop and ColStartStop arrays to control which points are "filled"
			if (j < ColStartStop[i][0] || j > ColStartStop[i][1])
				continue;
			if (ColStartStop[i][0] == -1 && ColStartStop[i][1] == -1)
				continue;
			
			// get center point
			if (ReadInternalElevationValue(i, j) < 0.0) {
				// look for valid neighbors
				ncount = 0;
				vector = 0;

				// look left
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 1;
						break;
					}
				}

				// look right
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 16;
						break;
					}
				}

				// look up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 64;
						break;
					}
				}

				// look down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 4;
						break;
					}
				}

				// look left and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 128;
						break;
					}
				}

				// look right and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 32;
						break;
					}
				}

				// look left and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 2;
						break;
					}
				}

				// look right and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 8;
						break;
					}
				}

				// we have to have valid cells on opposite sides of the target point to do interpolation
				if ((vector & 68) == 68 || (vector & 17) == 17 || (vector & 34) == 34 || (vector & 136) == 136) {
					if (ncount >= 4) {
						if (PeakRule == -1) {
							// average
							elevsum = 0.0;
							weightsum = 0.0;
							for (k = 0; k < ncount; k ++) {
								elevsum += values[k] * weights[k];
								weightsum += weights[k];
							}

							newelev = elevsum / weightsum;
						}
						else if (PeakRule == HIGHPEAKS) {
							// keep max value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = max(elevsum, values[k]);

							newelev = elevsum;
						}
						else {
							// keep min value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = min(elevsum, values[k]);

							newelev = elevsum;
						}
					}
					else if (ncount > 0) {
						// no peak preservation...just average the values
						elevsum = 0.0;
						weightsum = 0.0;
						for (k = 0; k < ncount; k ++) {
							elevsum += (values[k] * weights[k]);
							weightsum += weights[k];
						}

						newelev = elevsum / weightsum;
					}

					// assign new elevation
					if (ncount > 0) {
						if (Header.z_bytes == 0) 		// short
							((short**) TempElev)[i][j] = (short) newelev;
						else if (Header.z_bytes == 1) 		// 4-byte int
							((int**) TempElev)[i][j] = (int) newelev;
						else if (Header.z_bytes == 2) 		// float
							((float**) TempElev)[i][j] = (float) newelev;
						else if (Header.z_bytes == 3) 		// double
							((double**) TempElev)[i][j] = newelev;
					}
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// scan the rows
	for (j = 0; j < Header.points; j ++) {
		for (i = 0; i < Header.columns; i ++) {
			// use RowStartStop and ColStartStop arrays to control which points are "filled"
			if (i < RowStartStop[j][0] || i > RowStartStop[j][1])
				continue;
			if (RowStartStop[j][0] == -1 && RowStartStop[j][1] == -1)
				continue;
			
			// get center point
			if (ReadInternalElevationValue(i, j) < 0.0) {
				// look for valid neighbors
				ncount = 0;
				vector = 0;

				// look left
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 1;
						break;
					}
				}

				// look right
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 16;
						break;
					}
				}

				// look up 
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 64;
						break;
					}
				}

				// look down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / (double) k;
						ncount ++;
						vector |= 4;
						break;
					}
				}

				// look left and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 128;
						break;
					}
				}

				// look right and up
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j + k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 32;
						break;
					}
				}

				// look left and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i - k, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 2;
						break;
					}
				}

				// look right and down
				for (k = 1; k < maxcells; k ++) {
					elev = ReadInternalElevationValue(i + k, j - k);
					if (elev > 0.0) {
						values[ncount] = elev;
						weights[ncount] = 1.0 / ((double) k * 1.414);
						ncount ++;
						vector |= 8;
						break;
					}
				}

				// we have to have valid cells on opposite sides of the target point to do interpolation
				if ((vector & 68) == 68 || (vector & 17) == 17 || (vector & 34) == 34 || (vector & 136) == 136) {
					if (ncount >= 4) {
						if (PeakRule == -1) {
							// average
							elevsum = 0.0;
							weightsum = 0.0;
							for (k = 0; k < ncount; k ++) {
								elevsum += (values[k] * weights[k]);
								weightsum += weights[k];
							}

							newelev = elevsum / weightsum;
						}
						else if (PeakRule == HIGHPEAKS) {
							// keep max value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = max(elevsum, values[k]);

							newelev = elevsum;
						}
						else {
							// keep min value
							elevsum = values[0];
							for (k = 1; k < ncount; k ++)
								elevsum = min(elevsum, values[k]);

							newelev = elevsum;
						}
					}
					else if (ncount > 0) {
						// no peak preservation...just average the values
						elevsum = 0.0;
						weightsum = 0.0;
						for (k = 0; k < ncount; k ++) {
							elevsum += values[k] * weights[k];
							weightsum += weights[k];
						}

						newelev = elevsum / weightsum;
					}

					// assign new elevation
					if (ncount > 0) {
						if (Header.z_bytes == 0) 		// short
							((short**) TempElev)[i][j] = (short) newelev;
						else if (Header.z_bytes == 1) 		// 4-byte int
							((int**) TempElev)[i][j] = (int) newelev;
						else if (Header.z_bytes == 2) 		// float
							((float**) TempElev)[i][j] = (float) newelev;
						else if (Header.z_bytes == 3) 		// double
							((double**) TempElev)[i][j] = newelev;
					}
				}
			}
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	DeallocateIntArray(RowStartStop, Header.points);
	DeallocateIntArray(ColStartStop, Header.columns);
}

double PlansDTM::GetGridElevation(double x, double y, BOOL ConvertElevation)
{
	static long r, c;
	static double elev;

	if (!Valid)
		return(-1.0);

	// calculate row and column
	c = (long) floor((x - Header.origin_x) / Header.column_spacing);
	r = (long) floor((y - Header.origin_y) / Header.point_spacing);

	elev = GetGridElevation(static_cast<int>(c), static_cast<int>(r));

	if (ConvertElevation)
		elev = (elev - Header.min_z) * ElevationConversion * VerticalExaggeration + Header.min_z * ElevationConversion;

	return(elev);
}

BOOL PlansDTM::CreateTextureSurface(int FilterSize, int Method)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	int i, j, k, l;
	double weightsum;
	double* e;
	int nindex;		// neighbor index
	double valuesum;
	double WindowMean;
	double newelev;
	double WindowMin = DBL_MAX;
	double WindowMax = DBL_MIN;
	int FilterHalfSize = FilterSize / 2;
	
	// FilterSize must be odd
	if (FilterSize % 2 == 0)
		return(FALSE);

	// get memory for window values
	e = new double [FilterSize * FilterSize];

	// allocate memory for temp model...type matches dtm elevation type
	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points)) {
		delete [] e;
		return(FALSE);
	}

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// get neighbor pixels
			nindex = 0;
			valuesum = 0.0;
			weightsum = 0.0;
			for (k = -FilterHalfSize; k <= FilterHalfSize; k ++) {
				for (l = -FilterHalfSize; l <= FilterHalfSize; l ++) {
					e[nindex] = ReadInternalElevationValue(i - k, j - l);
					if (e[nindex] >= 0.0) {
						valuesum += e[nindex];
						weightsum += 1.0;
						WindowMin = min(e[nindex], WindowMin);
						WindowMax = max(e[nindex], WindowMax);
					}
					nindex ++;
				}
			}

			if (nindex && weightsum > 1.0) {
				// compute mean value for window
				WindowMean = valuesum / weightsum;

				valuesum = 0.0;
				weightsum = 0.0;
				for (k = 0; k < FilterSize * FilterSize; k ++) {
					if (e[k] >= 0.0) {
						valuesum += (e[k] - WindowMean) * (e[k] - WindowMean);
						weightsum += 1.0;
					}
				}

				// compute coefficient of variation
				if (WindowMean != 0.0)
					newelev = sqrt(valuesum / (weightsum - 1.0)) / WindowMean;
				else
					newelev = 0.0;
			}
			else
				newelev = -1.0;

			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = (short) newelev;
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = (int) newelev;
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = (float) newelev;
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = newelev;
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	delete [] e;

	return(TRUE);
}

BOOL PlansDTM::CreateTopoSurface(int Type)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	int i, j;
	double Gterm, Hterm;
	double newelev = 0.0;
	double elev4, elev6, elev2, elev8;
	
	// allocate memory for temp model...type matches dtm elevation type
	void** TempElev = NULL;
	if (!AllocateElevationMemory(TempElev, Header.z_bytes, Header.columns, Header.points)) {
		return(FALSE);
	}

	// copy original elevations to temp
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = ((short**) lpsElevData)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = ((int**) lpsElevData)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = ((float**) lpsElevData)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = ((double**) lpsElevData)[i][j];
		}
	}

	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			// get neighbor pixels
			elev4 = ReadInternalElevationValue(i - 1, j);
			elev6 = ReadInternalElevationValue(i + 1, j);
			elev2 = ReadInternalElevationValue(i, j + 1);
			elev8 = ReadInternalElevationValue(i, j - 1);

			if (elev2 >= 0.0 && elev8 >= 0.0 && elev4 >= 0.0 && elev6 >= 0.0) {
				Gterm = (-elev4 + elev6) / (2.0 * Header.column_spacing);
				Hterm = (elev2 - elev8) / (2.0 * Header.point_spacing);

				// compute slope
				if (Type == SLOPE)
					newelev = sqrt(Gterm * Gterm + Hterm * Hterm);

				// compute aspect
				if (Type == ASPECT)
					newelev = atan2(-Hterm, -Gterm) * (180.0 / 3.1415926535);
			}
			else
				newelev = -1.0;

			if (Header.z_bytes == 0) 		// short
				((short**) TempElev)[i][j] = (short) newelev;
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) TempElev)[i][j] = (int) newelev;
			else if (Header.z_bytes == 2) 		// float
				((float**) TempElev)[i][j] = (float) newelev;
			else if (Header.z_bytes == 3) 		// double
				((double**) TempElev)[i][j] = newelev;
		}
	}

	// set model elevations to temp elevations
	for (i = 0; i < Header.columns; i ++) {
		for (j = 0; j < Header.points; j ++) {
			if (Header.z_bytes == 0) 		// short
				((short**) lpsElevData)[i][j] = ((short**) TempElev)[i][j];
			else if (Header.z_bytes == 1) 		// 4-byte int
				((int**) lpsElevData)[i][j] = ((int**) TempElev)[i][j];
			else if (Header.z_bytes == 2) 		// float
				((float**) lpsElevData)[i][j] = ((float**) TempElev)[i][j];
			else if (Header.z_bytes == 3) 		// double
				((double**) lpsElevData)[i][j] = ((double**) TempElev)[i][j];
		}
	}

	// delete temp elevation model
	FreeElevationMemory(TempElev, Header.columns);

	return(TRUE);
}

double PlansDTM::ApplyBias(double Z)
{
	return(Z + Header.bias);
}

double PlansDTM::RemoveBias(double Z)
{
	return(Z - Header.bias);
}

double PlansDTM::Bias()
{
	return(Header.bias);
}
BOOL PlansDTM::UpdateModelHeader(LPCTSTR FileName)
{
	if (!Valid)
		return(FALSE);

	// open file and write header
	FILE* f;
	f = fopen((LPCTSTR) FileName, "rb+");
	if (f) {
		// jump to beginning
		fseek(f, 0, SEEK_SET);

		// force version to 3.1
		Header.version = 3.1f;

		// model is open
		fwrite(&Header.signature, sizeof(char), 21, f);
		fwrite(&Header.name, sizeof(char), 61, f);
		fwrite(&Header.version, sizeof(float), 1, f);
		fwrite(&Header.origin_x, sizeof(double), 1, f);
		fwrite(&Header.origin_y, sizeof(double), 1, f);
		fwrite(&Header.min_z, sizeof(double), 1, f);
		fwrite(&Header.max_z, sizeof(double), 1, f);
		fwrite(&Header.rotation, sizeof(double), 1, f);
		fwrite(&Header.column_spacing, sizeof(double), 1, f);
		fwrite(&Header.point_spacing, sizeof(double), 1, f);
		fwrite(&Header.columns, sizeof(long), 1, f);
		fwrite(&Header.points, sizeof(long), 1, f);
		fwrite(&Header.xy_units, sizeof(short), 1, f);
		fwrite(&Header.z_units, sizeof(short), 1, f);
		fwrite(&Header.z_bytes, sizeof(short), 1, f);
		fwrite(&Header.coord_sys, sizeof(short), 1, f);
		fwrite(&Header.coord_zone, sizeof(short), 1, f);
		fwrite(&Header.horizontal_datum, sizeof(short), 1, f);
		fwrite(&Header.vertical_datum, sizeof(short), 1, f);
		fwrite(&Header.bias, sizeof(double), 1, f);

		fclose (f);

		return(TRUE);
	}
	return(FALSE);
}

void PlansDTM::ChangeXYUnits(int Units)
{
	if (Valid) {
		Header.xy_units = Units;
	}
}

void PlansDTM::ChangeZUnits(int Units)
{
	if (Valid) {
		Header.z_units = Units;
	}
}

void PlansDTM::ChangeDescriptiveName(LPCTSTR NewName)
{
	if (Valid) {
		strcpy(Header.name, NewName);
	}
}

void PlansDTM::ChangeVerticalDatum(int Datum)
{
	if (Valid) {
		Header.vertical_datum = Datum;
	}
}

void PlansDTM::ChangeHorizontalDatum(int Datum)
{
	if (Valid) {
		Header.horizontal_datum = Datum;
	}
}

void PlansDTM::ChangeCoordinateZone(int CoordZone)
{
	if (Valid) {
		Header.coord_zone = CoordZone;
	}
}

void PlansDTM::ChangeCoordinateSystem(int CoordSys)
{
	if (Valid) {
		Header.coord_sys = CoordSys;
	}
}

void PlansDTM::ChangeMinMaxElevation(double MinZ, double MaxZ)
{
	if (Valid) {
		Header.min_z = MinZ;
		Header.max_z = MaxZ;
	}
}

BOOL PlansDTM::AllocateElevationMemory(void**& ElevData, int DataType, int Columns, int Rows, double InitialValue)
{
	int i, j;

	//// get amount of available memory
	//MEMORYSTATUS stat;
	//GlobalMemoryStatus(&stat);

	//// calculate how much memory is needed
	//SIZE_T memneeded = Columns * Rows;
	//if (DataType == 0)
	//	memneeded = memneeded * sizeof(short);
	//else if (DataType == 1)
	//	memneeded = memneeded * sizeof(int);
	//else if (DataType == 2)
	//	memneeded = memneeded * sizeof(float);
	//else if (DataType == 3)
	//	memneeded = memneeded * sizeof(double);
	//
	//// add fudge factor
	//memneeded = memneeded + (memneeded / 100);

	//// check available memory against needed memory
	//if (stat.dwAvailVirtual < memneeded) {
	//	return(FALSE);
	//}

	switch (DataType) {
		case 0:		// short
		default:
			// allocate memory for elevation data
			try {
				ElevData = (void**) new short* [Columns];
			}
			catch (...) {
				ElevData = NULL;
			}

			if (ElevData) {
				for (i = 0; i < Columns; i ++) {
					try {
						ElevData[i] = (void**) new short* [Rows];
					}
					catch (...) {
						ElevData[i] = NULL;
					}

					if (!ElevData[i]) {
						for (j = i - 1; j >= 0; j --)
							delete [] ElevData[j];

						delete [] ElevData;

						ElevData = NULL;
						break;
					}
				}
			}
			break;
		case 1:		// int
			// allocate memory for elevation data
			try {
				ElevData = (void**) new int* [Columns];
			}
			catch (...) {
				ElevData = NULL;
			}

			if (ElevData) {
				for (i = 0; i < Columns; i ++) {
					try {
						ElevData[i] = (void*) new int [Rows];
					}
					catch (...) {
						ElevData[i] = NULL;
					}

					if (!ElevData[i]) {
						for (j = i - 1; j >= 0; j --)
							delete [] ElevData[j];

						delete [] ElevData;

						ElevData = NULL;
						break;
					}
				}
			}
			break;
		case 2:		// float
			// allocate memory for elevation data
			try {
				ElevData = (void**) new float* [Columns];
			}
			catch (...) {
				ElevData = NULL;
			}
			

			if (ElevData) {
				for (i = 0; i < Columns; i ++) {
					try {
						ElevData[i] = (void*) new float [Rows];
					}
					catch (...) {
						ElevData[i] = NULL;
					}
					

					if (!ElevData[i]) {
						for (j = i - 1; j >= 0; j --)
							delete [] ElevData[j];

						delete [] ElevData;

						ElevData = NULL;
						break;
					}
				}
			}
			break;
		case 3:		// double
			// allocate memory for elevation data
			try {
				ElevData = (void**) new double* [Columns];
			}
			catch (...) {
				ElevData = NULL;
			}
			

			if (ElevData) {
				for (i = 0; i < Columns; i ++) {
					try {
						ElevData[i] = (void*) new double [Rows];
					}
					catch (...) {
						ElevData[i] = NULL;
					}
					

					if (!ElevData[i]) {
						for (j = i - 1; j >= 0; j --)
							delete [] ElevData[j];

						delete [] ElevData;

						ElevData = NULL;
						break;
					}
				}
			}
			break;
	}

	if (!ElevData)
		return(FALSE);

	// set initial values
	for (i = 0; i < Columns; i ++) {
		for (j = 0; j < Rows; j ++) {
			switch (DataType) {
				case 0:		// short
				default:
					((short**) ElevData)[i][j] = (short) InitialValue;
					break;
				case 1:		// int (4 byte)
					((int**) ElevData)[i][j] = (int) InitialValue;
					break;
				case 2:		// float
					((float**) ElevData)[i][j] = (float) InitialValue;
					break;
				case 3:		// double
					((double**) ElevData)[i][j] = InitialValue;
					break;
			}
		}
	}

	return(TRUE);
}

void PlansDTM::FreeElevationMemory(void**& ElevData, int Columns)
{
	for (int i = 0; i < Columns; i ++)
		delete [] ElevData[i];
	
	delete [] ElevData;

	ElevData = NULL;
}

int PlansDTM::ScanModelForVoidAreas(void)
{
	if (!Valid)
		return(-1);

	int count = 0;		// count of void values
	void* elevdata = NULL;		// pointer to elevation data
	int j;

	if (OpenModelFile()) {
		// set up memory for a single profile
		if (Header.z_bytes == 0) {			// short
			try {
				elevdata = (void*) new short [Header.points];
			}
			catch (...) {
				elevdata = NULL;
			}
			
		}
		else if (Header.z_bytes == 1) {		// int
			try {
				elevdata = (void*) new int [Header.points];
			}
			catch (...) {
				elevdata = NULL;
			}
			
		}
		if (Header.z_bytes == 2) {			// float
			try {
			elevdata = (void*) new float [Header.points];
			}
			catch (...) {
				elevdata = NULL;
			}
			
		}
		if (Header.z_bytes == 3) {			// double
			try {
				elevdata = (void*) new double [Header.points];
			}
			catch (...) {
				elevdata = NULL;
			}
			
		}

		if (!elevdata)
			return(-1);

		// read through model profile by profile and look for void values
		for (long i = 0; i < Header.columns; i ++) {
			LoadDTMProfile(i, elevdata);

			for (j = 0; j < Header.points; j ++) {
				if (Header.z_bytes == 0) {			// short
					if (((short *)elevdata)[j] < 0)
						count ++;
				}
				else if (Header.z_bytes == 1) {		// int
					if (((int *)elevdata)[j] < 0)
						count ++;
				}
				if (Header.z_bytes == 2) {			// float
					if (((float *)elevdata)[j] < 0.0)
						count ++;
				}
				if (Header.z_bytes == 3) {			// double
					if (((double *)elevdata)[j] < 0.0)
						count ++;
				}
			}
		}

		// delete memory
		delete [] elevdata;

		CloseModelFile();
	}
	else
		count = -1;

	return(count);
}

BOOL PlansDTM::WriteENVIFile(LPCTSTR FileName, LPCTSTR HeaderFileName, int Hemisphere)
{
	if (!Valid)
		return(FALSE);

	int i, j;
	char* units[] = {"feet", "meters", "unknown"};
	char* hemispheres[] = {"North", "South"};
	short snull = -9999;
	int inull = -9999;
	float fnull = -9999.0f;
	double dnull = -9999.0;

	// open output file for data
	FILE* f = fopen(FileName, "wb");
	if (f) {
		// write data
		if (HaveElevations) {
			for (j = Header.points - 1; j >= 0; j --) {
				for (i = 0; i < Header.columns; i ++) {
					if (Header.z_bytes == 0) {		// short
						if (((short**)lpsElevData)[i][j] < 0)
							fwrite(&snull, sizeof(short), (size_t) 1, f);
						else
							fwrite(&((short**)lpsElevData)[i][j], sizeof(short), (size_t) 1, f);
					}
					else if (Header.z_bytes == 1) {		// 4-byte int
						if (((int**)lpsElevData)[i][j] < 0)
							fwrite(&inull, sizeof(int), (size_t) 1, f);
						else
							fwrite(&((int**)lpsElevData)[i][j], sizeof(int), (size_t) 1, f);
					}
					else if (Header.z_bytes == 2) {		// float
						if (((float**)lpsElevData)[i][j] < 0.0f)
							fwrite(&fnull, sizeof(float), (size_t) 1, f);
						else
							fwrite(&((float**)lpsElevData)[i][j], sizeof(float), (size_t) 1, f);
					}
					else if (Header.z_bytes == 3) {		// double
						if (((double**)lpsElevData)[i][j] < 0.0)
							fwrite(&dnull, sizeof(double), (size_t) 1, f);
						else
							fwrite(&((double**)lpsElevData)[i][j], sizeof(double), (size_t) 1, f);
					}
				}
			}
		}
		else {
			// allocate memory for a single column
			void** elev_column;
			if (AllocateElevationMemory(elev_column, Header.z_bytes, 1, Header.points)) {
				// open model file
				OpenModelFile();

				// load columns and write to output file
				for (i = 0; i < Header.columns; i ++) {
					// load column
					LoadDTMProfile(i, elev_column[0]);

					// write to output...have to skip through file
					__int64 startoffset = 0l;
					__int64 offsetforrow = 0l;
//					long startoffset = 0l;
//					long offsetforrow = 0l;

					// compute offset from end of pixel in row n to start of pixel in row n+1
					if (Header.z_bytes == 0) {		// short
						startoffset = sizeof(short) * (__int64) i;
						offsetforrow = sizeof(short) * ((__int64)Header.columns - (__int64)1);
					}
					else if (Header.z_bytes == 1) {		// 4-byte int
						startoffset = sizeof(int) * (__int64)i;
						offsetforrow = sizeof(int) * ((__int64)Header.columns - (__int64)1);
					}
					else if (Header.z_bytes == 2) {		// float
						startoffset = sizeof(float) * (__int64)i;
						offsetforrow = sizeof(float) * ((__int64)Header.columns - (__int64)1);
					}
					else if (Header.z_bytes == 3) {		// double
						startoffset = sizeof(double) * (__int64)i;
						offsetforrow = sizeof(double) * ((__int64)Header.columns - (__int64)1);
					}

					// jump to first location...from file beginning
					_fseeki64(f, startoffset, SEEK_SET);
//					fseek(f, startoffset, SEEK_SET);

					for (j = Header.points - 1; j >= 0; j --) {
						if (Header.z_bytes == 0) {		// short
							if (((short**)elev_column)[0][j] < 0)
								fwrite(&snull, sizeof(short), (size_t) 1, f);
							else
								fwrite(&((short**)elev_column)[0][j], sizeof(short), (size_t) 1, f);
						}
						else if (Header.z_bytes == 1) {		// 4-byte int
							if (((int**)elev_column)[0][j] < 0)
								fwrite(&inull, sizeof(int), (size_t) 1, f);
							else
								fwrite(&((int**)elev_column)[0][j], sizeof(int), (size_t) 1, f);
						}
						else if (Header.z_bytes == 2) {		// float
							if (((float**)elev_column)[0][j] < 0)
								fwrite(&fnull, sizeof(float), (size_t) 1, f);
							else
								fwrite(&((float**)elev_column)[0][j], sizeof(float), (size_t) 1, f);
						}
						else if (Header.z_bytes == 3) {		// double
							if (((double**)elev_column)[0][j] < 0)
								fwrite(&dnull, sizeof(double), (size_t) 1, f);
							else
								fwrite(&((double**)elev_column)[0][j], sizeof(double), (size_t) 1, f);
						}

						// jump to next location...except last value
						if (j) {
							_fseeki64(f, offsetforrow, SEEK_CUR);
//							fseek(f, offsetforrow, SEEK_CUR);
						}
					}
				}

				// close model file
				CloseModelFile();

				FreeElevationMemory(elev_column, 1);
			}
			else {
				fclose(f);
				DeleteFile(FileName);
				return(FALSE);
			}
		}

		fclose(f);

		// write header file...same name as data file but append .hdr extension
		int datatype = 0;
		if (Header.z_bytes == 0) 		// short
			datatype = 2;		// 2-byte signed int
		else if (Header.z_bytes == 1) 		// 4-byte int
			datatype = 3;		// 4-byte signed int
		else if (Header.z_bytes == 2) 		// float
			datatype = 4;		// 4-byte float
		else if (Header.z_bytes == 3) 		// double
			datatype = 5;		// 8-byte double

		f = fopen(HeaderFileName, "wt");
		if (f) {
			char mapinfo[1024];

			// format the georeferencing information
			if (Header.coord_sys == 1) {		// UTM
				if (Header.horizontal_datum == 2) { // NAD 83
					sprintf(mapinfo, "{UTM, 1, 1, %.6lf, %.6lf, %.6lf, %.6lf, %i, %s, North America 1983, units=%s}",
						Header.origin_x,
						Header.origin_y + (double) (Header.points - 1) * Header.point_spacing,
						Header.column_spacing,
						Header.point_spacing,
						Header.coord_zone,
						hemispheres[Hemisphere],
						units[Header.xy_units]);
				}
				else { // unknown or NAD 27
					sprintf(mapinfo, "{UTM, 1, 1, %.6lf, %.6lf, %.6lf, %.6lf, %i, %s, North America 1927, units=%s}",
						Header.origin_x,
						Header.origin_y + (double) (Header.points - 1) * Header.point_spacing,
						Header.column_spacing,
						Header.point_spacing,
						Header.coord_zone,
						hemispheres[Hemisphere],
						units[Header.xy_units]);
				}
			}
			else if (Header.coord_sys == 2) {	// state plane
				if (Header.horizontal_datum == 2) { // NAD 83
					sprintf(mapinfo, "{State Plane (NAD 83), 1, 1, %.6lf, %.6lf, %.6lf, %.6lf, %i, units=%s}",
						Header.origin_x,
						Header.origin_y + (double) (Header.points - 1) * Header.point_spacing,
						Header.column_spacing,
						Header.point_spacing,
						Header.coord_zone,
						units[Header.xy_units]);
				}
				else { // unknown or NAD 27
					sprintf(mapinfo, "{State Plane (NAD 27), 1, 1, %.6lf, %.6lf, %.6lf, %.6lf, %i, units=%s}",
						Header.origin_x,
						Header.origin_y + (double) (Header.points - 1) * Header.point_spacing,
						Header.column_spacing,
						Header.point_spacing,
						Header.coord_zone,
						units[Header.xy_units]);
				}
			}
			else  {	// unknown (0) or other
				sprintf(mapinfo, "{Unknown, 1, 1, %.6lf, %.6lf, %.6lf, %.6lf, %i}",
					Header.origin_x,
					Header.origin_y + (double) (Header.points - 1) * Header.point_spacing,
					Header.column_spacing,
					Header.point_spacing,
					Header.coord_zone);
			}
			fprintf(f, "ENVI\n");
			fprintf(f, "samples = %i\n", Header.columns);
			fprintf(f, "lines = %i\n", Header.points);
			fprintf(f, "bands = 1\n");
			fprintf(f, "interleave = bsq\n");
			fprintf(f, "data type = %i\n", datatype);
			fprintf(f, "byte order = 0\n");		// little endian
			fprintf(f, "header offset = 0\n");
			fprintf(f, "data ignore value = -9999\n");
			fprintf(f, "map info = %s\n", mapinfo);
			fprintf(f, "file type = ENVI Standard\n");

			// close header file
			fclose(f);

			// successful
			return(TRUE);
		}
		else {
			// couldn't write the header file...delete data file
			DeleteFile(FileName);
		}
	}
	return(FALSE);
}

BOOL PlansDTM::WriteASCIICSV(LPCTSTR FileName, double Zfactor)
{
	if (!Valid || !HaveElevations)
		return(FALSE);

	FILE* f = fopen(FileName, "wt");
	if (f) {
		int i, j;
		double pz;

		// write header with column numbers
		for (i = 0; i < Header.columns; i ++) {
			if (i == 0)
				fprintf(f, "Col%i", i + 1);
			else
				fprintf(f, ",Col%i", i + 1);
		}
		fprintf(f, "\n");


		for (j = Header.points - 1; j >= 0; j --) {
			for (i = 0; i < Header.columns; i ++) {
				pz = ReadInternalElevationValue(i, j);
				if (i == 0) {
					if (pz < 0.0) {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, "%.0lf", -32767.0);
						else
							fprintf(f, "%.6lf", -32767.0);
					}
					else {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, "%.0lf", pz * Zfactor);
						else
							fprintf(f, "%.6lf", pz * Zfactor);
					}
				}
				else {
					if (pz < 0.0) {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, ",%.0lf", -32767.0);
						else
							fprintf(f, ",%.6lf", -32767.0);
					}
					else {
						if (Header.z_bytes == 0 || Header.z_bytes == 1)
							fprintf(f, ",%.0lf", pz * Zfactor);
						else
							fprintf(f, ",%.6lf", pz * Zfactor);
					}
				}
			}
			fprintf(f, "\n");
		}

		fclose(f);
		return(TRUE);
	}
	return(FALSE);
}

int ** PlansDTM::AllocateIntArray(int Columns, int Rows)
{
	int ** lpsData = NULL;
	try {
		lpsData = (int**) new int* [Columns];
	}
	catch (...) {
		lpsData = NULL;
	}
	

	if (lpsData) {
		for (int i = 0; i < Columns; i ++) {
			try {
				lpsData[i] = (int*) new int [Rows];
			}
			catch (...) {
				lpsData[i] = NULL;
			}
			

			if (!lpsData[i]) {
				for (int j = i - 1; j >= 0; j --)
					delete [] lpsData[j];

				delete [] lpsData;
				lpsData = NULL;
			}
		}
	}
	return(lpsData);
}

void PlansDTM::DeallocateIntArray(int** lpsData, int Columns)
{
	if (lpsData) {
		for (int i = 0; i < Columns; i ++)
			delete [] lpsData[i];

		delete [] lpsData;
	}
}

BOOL PlansDTM::ElevationsAreLoaded()
{
	return(HaveElevations && Valid);
}

BOOL PlansDTM::CreateNewModel(double OriginX, double OriginY, int Cols, int Rows, double ColSpacing, double RowSpacing, int XYunits, int Zunits, int Zbytes, int CoordSystem, int CoordZone, int Hdatum, int Zdatum, int Elevation)
{
	// make sure we don't already have a valid model...if so delete elevation data
	if (Valid)
		Destroy();

	// populate header
	Header.columns = Cols;
	Header.points = Rows;
	Header.column_spacing = ColSpacing;
	Header.point_spacing = RowSpacing;
	Header.origin_x = OriginX;
	Header.origin_y = OriginY;
	Header.rotation = 0.0;
	Header.version = 3.1f;
	Header.xy_units = XYunits;
	Header.z_units = Zunits;
	Header.z_bytes = Zbytes;
	Header.coord_sys = CoordSystem;
	Header.coord_zone = CoordZone;
	Header.horizontal_datum = Hdatum;
	Header.vertical_datum = Zdatum;
	Header.bias = 0.0;
	Header.max_z = Header.min_z = (double) Elevation;
	strcpy(Header.name, "Temporary model");
	strcpy(Header.signature, "PLANS-PC BINARY .DTM");

	// allocate elevation memory and set initial value
	if (AllocateElevationMemory(lpsElevData, Zbytes, Cols, Rows, Elevation)) {
		// set flags
		Valid = TRUE;
		HaveElevations = TRUE;
		UsingPatchElev = FALSE;
	}

	return(Valid);
}
