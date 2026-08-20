// LidarData.cpp: implementation of the CLidarData class.
//
//////////////////////////////////////////////////////////////////////
// NOTE: when updating the version of LASzip.dll, you must update the referencel to laszip_dll.c and make sure that this file (laszip_dll.c) is
// not using precompiled headers
//
// 11/9/2006
// added code to preserve LAS return info not included in LDA return record when converting
// a CLIDARData file to LAS (only if original file was LAS format)...still not perfect but a start at
// switching over to use LAS files for all FUSION tasks
//
// 1/17/2012
// modified the logic used to set a larger buffer for point data files. previous logic wasn't really being
// applied in a way that help with reading point data
//
// 8/6/2012
// modified to use LASlib and to support LAZ files. all code changes are bracketed by #ifdef USE_LASLIB...#endif
// original funtionality should be preserved 
//
// 3/14/13
// lots of minor code tweeks to fix a problem with random failure to open files and detect format. turns out the 
// problem was OS related and was "fixed" after rebooting.
//
// removed all of the changes...I think
//
// 8/2013
// lots of changes related to use of LASzip.dll. Added m_FileName, m_IsCompressed and IsCompressed()
//
// Also removed an "extra" call to fclose() in Open(). This should have been causing an error but 
// attempting to close a stream that is already closed just returns EOF so unless you check for the error (I don't) it shouldn't
// cause any trouble.
//
// 10/31/2013
// Modified ConvertToLAS() to output only points with specific classification values. This allows calling programs to remove
// points with specific LAS classification values. For example, outliers (class=7). This works well in conjunction with the CleanLASPoints
// option to remove LAS points that do not comply with the LAS specification.
//
// 8/25/2015
// Added code to help with LAS version 1.4 files. If there are fewer than (2^32 - 1) total returns in a file identifier as version 1.4+,
// I copy the total return count and the counts for the first 5 returns from the "extended" variables in the LAS header into the "legacy"
// variables. LASzip.dll doesn't seem to do this. Vendors have the option of maintaining compatability by duplicating the extended variables
// but I have some files where this was not done.
//
// 12/2/2015
// Fixed some problems reading LDA format files when the LASzip.dll is loaded. The ability to read LDA has been broken for some time...probably 
// realted to changes to extract individual flightlines from a file but I don't have a record of when the flightline capability was added.
//
// 12/20/2016
// Fixed some issues with the header being written through the LASzip dll. Problems are related to version-specific fields and the FileSourceID
// and GlobalEncoding fields. Also modified the logic used when working with V1.4 LAS files to correct rpoblems related to writing V1.4 files.
//
// 2/3/2017
// Added the function CopyEVLRs() to help with LS version 1.4-related code changes. This function is not part of a class and is intended to be called
// when both the source and destination files are closed. the function test for LAS format, version, and the presence of EVLRs.
//
// 5/4/2018
// More changes to deal with LAS 1.4 and the 4 bit return numbers and 4 bit number of returns in the pulse and the use of the overlap bit.
//
//	8/6/2018
//	Corrected a problem when determining the min/max values for X, Y & Z in data files. Variables for the max values were being initialized to a vary 
//	small positive number. This worked fine unless the coordinates were all negative. For negative coordinates, the maximum values reported were always 0.0.
//
//	9/11/2018
//	added support for large files when copying EVLRs using _fseeki64 and _ftelli64. Previous code would work until the file exceeded 2Gb, then the EVLR would
//	be corrupted (and probably data in the file as well)
//
//	1/28/2019
//	changed the logic used for dealing with classification flags for different point formats and added functions to report synthetic and key-point flag values
//	(LastPointIsKeypoint() and LastPointIsSynthetic())
//
//	1/10/2020
//	Added capability to slice based on GPS time to ConvertToLAS()
//
//	5/19/2020
//	Fixed an error with the LastPointIsWithheld(), LastPointIsSynthetic(), and LastPointIsKeypoint() functions when using the LASzip.dll.
//	For point record formats 0-5, code was using the classification field as if it contained 8 bits...it doesn't. Previous versions work
//	correctly.Only beta release versions since January 28, 2019 were affected and there was only a problem with data that has points
//	marked as synthetic, overlap, or withheld.
//
//  2/28/2024
//	Fixed a problem where the extended_scan_angle for point records > 5 was not being used to populate the nadirangle for a point. This lead
//	to a divide-by-zero error when coloring point using the scan angle.
//
// 5/21/2024
//	Fixed a problem related to use of laszip.dll and differences in how classification flags for points are handled for LAS format 1.4 files
//	relative to earlier formats. Even when reading and writing V1.4 files, the dll requires that classification flags for points be set
//	for variables involved in writing pre-V1.4 files. If the flags for V1.4 do not match those for earlier format variables, the dll throws
//	an error. In fixing the problem in FUSION tools, I also discovered a bug in the laszip.dll code that involved incorrect access to memory
//	when checking the flag values. Bug won't necessarily cause trouble but is not the best programming practice.
//
//	I also fixed a problem when writing V1.4 files with more than 5 returns per pulse. Previous code was not correctly updating the number of
//	returns for returns above 5. This problem only affected LDA2LAS. All other programs used the correct logic for the various versions of LAS
//	format files.
//

//#define USE_LASLIB			// comment out to use original non-laslib code...as of 8/2013 I don't think the LASLIB code will work correctly

#include "stdafx.h"
#include "../../FUSION/versionID.h"
#include "LidarData_LASlib.h"
#include "DataIndex.h"
#include "math.h"
#include <float.h>
#include <io.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//#define DLL_DIAGNOSTIC

// added 12/21/2016
#define I64_FLOOR(n) ((((__int64)(n)) > (n)) ? (((__int64)(n))-1) : ((__int64)(n)))
#define I32_QUANTIZE(n) (((n) >= 0) ? (int)((n)+0.5f) : (int)((n)-0.5f))

#define FILE_BUFFER_SIZE 32766

// function is not part of the CLidarData class but is used to copy EVLRs from one file to another
BOOL CopyEVLRs(LPCTSTR SourceFileName, LPCTSTR DestinationFileName, int RecordTypes)
{
	BOOL retcode = FALSE;
	unsigned long NumberofEVLRs = 0;
	unsigned long CopyCnt = 0;
	__int64 StartOfEVLR = 0;
	__int64 OffsetToEVLRs = 0;
	CLASExtendedVariableLengthRecord EVLRHeader;
	int ReadCnt = 0;
	int WriteCnt = 0;
	unsigned char c;

	// open the source file...may use native FUSION code or laszip.dll code
	CLidarData dat(SourceFileName);
	if (dat.IsValid()) {
		// check the format...must be LAS
		if (dat.GetFileFormat() == LASDATA) {
			// check to see if version is LAS 1.4+ and there are any EVLRs...if not, nothing to do
			if (dat.m_HaveLASZIP_DLL) {
				if (dat.lasdll_header->version_major == 1 && dat.lasdll_header->version_minor >= 4 && dat.lasdll_header->number_of_extended_variable_length_records > 0) {
					retcode = TRUE;

					NumberofEVLRs = dat.lasdll_header->number_of_extended_variable_length_records;
					StartOfEVLR = dat.lasdll_header->start_of_first_extended_variable_length_record;
				}
			}
			else {
				if (dat.m_LASFile.Header.VersionMajor == 1 && dat.m_LASFile.Header.VersionMinor >= 4 && dat.m_LASFile.Header.NumberOfExtendedVLRs > 0) {
					retcode = TRUE;

					NumberofEVLRs = dat.m_LASFile.Header.NumberOfExtendedVLRs;
					StartOfEVLR = dat.m_LASFile.Header.StartOfExtendedVLR;
				}
			}
		}
		dat.Close();
	}

	if (retcode) {
		// open output file to check version...must also be LAS 1.4+
		dat.Open(DestinationFileName);
		if (dat.IsValid()) {
			if (dat.m_HaveLASZIP_DLL) {
				if (dat.lasdll_header->version_major == 1 && dat.lasdll_header->version_minor >= 4) {
					retcode = TRUE;
				}
				else
					retcode = FALSE;
			}
			else {
				if (dat.m_LASFile.Header.VersionMajor == 1 && dat.m_LASFile.Header.VersionMinor >= 4) {
					retcode = TRUE;
				}
				else
					retcode = FALSE;
			}

			// close output file...EVLRs will be copied directly to the end of the file
			dat.Close();
		}
	}

	if (retcode) {
		// open input file for reading
		FILE* InputFileHandle = fopen(SourceFileName, "rb");
		if (InputFileHandle) {
			// open output file for appending
			FILE* OutputFileHandle = fopen(DestinationFileName, "rb+");
			if (OutputFileHandle) {
				// seek to the end of the file
				_fseeki64(OutputFileHandle, 0, SEEK_END);

				// get current position in output file...this is the offset to the first EVLR header
				OffsetToEVLRs = (__int64) _ftelli64(OutputFileHandle);

				// jump to first EVLR in input file
				_fseeki64(InputFileHandle, StartOfEVLR, SEEK_SET);

				// read through EVLRs and copy them to the output file
				for (unsigned long i = 0; i < NumberofEVLRs; i ++) {
					ReadCnt = 0;
					WriteCnt = 0;

					if (EVLRHeader.Read(InputFileHandle)) {
						// make sure this is a record we are copying...do all except waveform data packet
						// 5/16/2024 added logic to omit COPC hierachy EVLR
						if (EVLRHeader.RecordID < 0xFFFF && EVLRHeader.RecordID != 1000) {
							// write header to output file
							if (EVLRHeader.WriteHeader(OutputFileHandle)) {
								// copy EVLR data block
								for (int i = 0; i < EVLRHeader.RecordLengthAfterHeader; i ++) {
									ReadCnt += (int) fread(&c, sizeof(unsigned char), 1, InputFileHandle);
									WriteCnt += (int) fwrite(&c, sizeof(unsigned char), 1, OutputFileHandle);
								}
								
								if (ReadCnt != WriteCnt) {
									// something went wrong...file is likely corrupted
									retcode = FALSE;
									break;
								}
								CopyCnt ++;
							}
						}
						else {
							// jump to end of record in input file
							_fseeki64(InputFileHandle, EVLRHeader.RecordLengthAfterHeader, SEEK_CUR);
						}
					}
					else {
						// could not read EVLR header...something is wrong...file will be corrupt if this is not the first EVLR
						retcode = FALSE;
						break;
					}
				}

				// see if we copied any EVLRs
				if (CopyCnt) {
					_fseeki64(OutputFileHandle, 235, SEEK_SET);
					fwrite(&OffsetToEVLRs, sizeof(__int64), 1, OutputFileHandle);			// start of first EVLR
					fwrite(&CopyCnt, sizeof(unsigned long), 1, OutputFileHandle);					// number of EVLRs
				}
				else {
					// something failed so we need to zero out the header variables
					OffsetToEVLRs = 0;

					_fseeki64(OutputFileHandle, 235, SEEK_SET);
					fwrite(&OffsetToEVLRs, sizeof(__int64), 1, OutputFileHandle);			// start of first EVLR
					fwrite(&CopyCnt, sizeof(unsigned long), 1, OutputFileHandle);					// number of EVLRs
				}

				fclose(OutputFileHandle);
			}
			else
				retcode = FALSE;

			fclose(InputFileHandle);
		}
		else
			retcode = FALSE;
	}

	return(retcode);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLidarData::CLidarData()
{
	m_FileFormat = INVALID;
	m_Valid = FALSE;
	m_ReasonForInvalidFile = NOFILEERROR;
	m_IsCompressed = FALSE;
	m_IsCOPCFormat = FALSE;
	m_HaveLocalBuffer = FALSE;
	m_BinaryFileBuffer = NULL;
	m_FileName.Empty();
	m_LineBuffer[0] = 0;
	buf2[0] = 0;
	m_COPCPointIndex = -1;
	m_COPCCurrentChunk = -1;

#ifdef USE_LASLIB
	lasreader = NULL;
#else
	lasdll_reader = NULL;
	lasdll_header = NULL;
	lasdll_point = NULL;
#endif

	m_BinaryFile = NULL;

	m_HaveLASZIP_DLL = FALSE;
	m_Version = 0.0f;

	CheckForLASZIP_DLL();
}

CLidarData::CLidarData(LPCTSTR szFileName)
{
	m_FileFormat = INVALID;
	m_Valid = FALSE;
	m_ReasonForInvalidFile = NOFILEERROR;
	m_IsCompressed = FALSE;
	m_IsCOPCFormat = FALSE;
	m_HaveLocalBuffer = FALSE;
	m_BinaryFileBuffer = NULL;
	m_FileName.Empty();
	m_LineBuffer[0] = 0;
	buf2[0] = 0;
	m_COPCPointIndex = -1;
	m_COPCCurrentChunk = -1;

#ifdef USE_LASLIB
	lasreader = NULL;
#else
	lasdll_reader = NULL;
	lasdll_header = NULL;
	lasdll_point = NULL;
#endif

	m_BinaryFile = NULL;
	
	m_HaveLASZIP_DLL = FALSE;
	m_Version = 0.0f;

	CheckForLASZIP_DLL();

	Open(szFileName);
}

BOOL CLidarData::CheckForLASZIP_DLL()
{
#if defined(_M_X64)
	m_HaveLASZIP_DLL = (GetModuleHandle("LASzip64.dll") != NULL);
#elif defined(_M_IX86)
	m_HaveLASZIP_DLL = (GetModuleHandle("LASzip.dll") != NULL);
#endif

	return(m_HaveLASZIP_DLL);
}

CLidarData::~CLidarData()
{
	Close();
}

BOOL CLidarData::IsValid()
{
	return(m_Valid);
}

BOOL CLidarData::IsCompressed()
{
	if (m_Valid) {
		if (m_HaveLASZIP_DLL)
			return(m_IsCompressed);
		else {
			// check for file extension
			CString csTemp = m_FileName;
			csTemp.MakeLower();
			if (csTemp.Find(".laz") >= 0)
				return(TRUE);
		}
	}
	return(FALSE);
}

BOOL CLidarData::Open(LPCTSTR szFileName)
{
	char signature[9];

	if (m_Valid)
		Close();

	// determine file format by looking at first 8 bytes...ASCII or binary
	FILE* f = fopen(szFileName, "rb");

	if (f) {
		if (fread(signature, sizeof(char), 8, f) == 8) {
			signature[8] = '\0';

			if (strcmp(signature, "LIDARDAT") == 0) {
				// if ASCII use CDataFile member to access
				m_FileFormat = ASCIIDATA;

				// read version
				if (fscanf(f, "%f", &m_Version) != 1) {
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADFORMAT;
				}
			}
			else if (strcmp(signature, "LIDARBIN") == 0) {
				// if binary, use direct access
				m_FileFormat = BINDATA;

				// read version...two parts
				int major, minor;
				fread(&major, sizeof(int), 1, f);
				fread(&minor, sizeof(int), 1, f);
				m_Version = (float) major + (float) minor / 10.0f;
			}
			else {
				// look at first 4 characters for LAS format signature...could be LAS or LAZ
				signature[4] = '\0';
				if (strcmp(signature, "LASF") == 0) {
					m_FileFormat = LASDATA;
					m_Version = 1.0f;
				}
				else {
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADFORMAT;
				}
			}
//			fclose(f);
		}
		else {
			m_FileFormat = INVALID;
			m_ReasonForInvalidFile = BADFORMAT;
		}
		fclose(f);
	}
	else {
		// can't open file...check to see if file exists
		if (_access(szFileName, 0) != 0)
			m_ReasonForInvalidFile = NOTEXIST;
		else
			m_ReasonForInvalidFile = CANTOPEN;

		m_FileFormat = INVALID;
	}

	// check for attempt to open compressed LAS file but without LASzip.dll available
	if (m_FileFormat == LASDATA) {
		// check for .laz extension
		CString csTemp = szFileName;
		csTemp.MakeLower();
		if (csTemp.Find(".laz") >= 0 && !m_HaveLASZIP_DLL) {
			// error...can't read LAZ without LASzip.dll
			m_FileFormat = INVALID;
			m_ReasonForInvalidFile = LAZNODLL;
		}

		// see if we have COPC format
		m_IsCOPCFormat = m_COPCIndex.IsCOPCFile(szFileName);
		if (m_IsCOPCFormat) {
			if (!m_HaveLASZIP_DLL) {
				// error...can't read LAZ/COPC without LASzip.dll
				m_FileFormat = INVALID;
				m_ReasonForInvalidFile = LAZNODLL;
			}
			else {
				m_COPCIndex.ReadIndex(szFileName);
				if (!m_COPCIndex.IsValid()) {
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADCOPCINDEX;
				}
			}
		}


	}

	if (m_FileFormat != INVALID) {
		if (m_FileFormat == ASCIIDATA) {
			m_ASCIIFile.Open(szFileName);

			if (m_ASCIIFile.IsValid()) {
				// read first line
				m_ASCIIFile.ReadASCIILine(m_LineBuffer);
			}
			else {
				m_FileFormat = INVALID;
				m_ReasonForInvalidFile = NODATA;
			}
		}
		else if (m_FileFormat == BINDATA) {
			m_BinaryFile = fopen(szFileName, "rb");
			if (m_BinaryFile) {
				// code to create larger read buffer
				m_BinaryFileBuffer = new char[FILE_BUFFER_SIZE] ();
				if (m_BinaryFileBuffer) {
					if (setvbuf(m_BinaryFile, m_BinaryFileBuffer, _IOFBF, FILE_BUFFER_SIZE) == 0)
						m_HaveLocalBuffer = TRUE;
					else {
						delete [] m_BinaryFileBuffer;
						m_HaveLocalBuffer = FALSE;
					}
				}
				// end of code

				fseek(m_BinaryFile, 16, SEEK_SET);
			}
			else {
				m_FileFormat = INVALID;
				m_ReasonForInvalidFile = CANTOPEN;
			}
		}
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			lasreadopener.set_file_name(szFileName);
			lasreader = lasreadopener.open();
			if (lasreader != 0) {
				m_Version = (float) lasreader->header.version_major + (float) lasreader->header.version_minor / 10.0f;
				lasreader->seek(0);		// first point, index == 0
			}
			else {
				m_FileFormat = INVALID;
				m_ReasonForInvalidFile = BADFORMAT;
			}
#else
			if (m_HaveLASZIP_DLL) {
				// create the reader
				if (laszip_create(&lasdll_reader)) {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: creating laszip reader\n");
#endif
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADFORMAT;
				}
				else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: created reader\n");
#endif
				}

				// open the reader
				laszip_BOOL is_compressed;
				if (laszip_open_reader(lasdll_reader, szFileName, &is_compressed)) {
					laszip_CHAR* error;
					if (laszip_get_error(lasdll_reader, &error)) {
						fprintf(stderr,"LASzip DLL error: cannot retrieve error messages\n");
					}
					fprintf(stderr,"LASzip DLL error: %s\n", error);

					laszip_destroy(lasdll_reader);
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADFORMAT;
				}
				else {
					m_IsCompressed = is_compressed == 1;
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: opened reader for file: %s\n", szFileName);
#endif
				}

				// get a pointer to the header
				if (m_FileFormat != INVALID) {
					if (!laszip_get_header_pointer(lasdll_reader, &lasdll_header)) {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: have pointer to header data\n");
#endif
						// get version info
						m_Version = (float) lasdll_header->version_major + (float) lasdll_header->version_minor / 10.0f;

						// update total return count and counts by return number for version 1.4+
						// this allows us to transfer these values to the FUSION header
						if (lasdll_header->version_major == 1 && lasdll_header->version_minor > 3) {
							if (lasdll_header->number_of_point_records == 0 && lasdll_header->extended_number_of_point_records < 4294967295) {
								lasdll_header->number_of_point_records = (int) lasdll_header->extended_number_of_point_records;
								for (int i = 0; i < 5; i ++) {
									lasdll_header->number_of_points_by_return[i] = (int) lasdll_header->extended_number_of_points_by_return[i];
								}
							}
						}

						// get a pointer to the point data
						if (laszip_get_point_pointer(lasdll_reader, &lasdll_point)) {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: getting point pointer from laszip reader\n");
#endif
							m_FileFormat = INVALID;
							m_ReasonForInvalidFile = BADFORMAT;
						}
						else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: have pointer to point data\n");
#endif
						}
					}
					else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: getting header pointer from laszip reader\n");
#endif
						m_FileFormat = INVALID;
						m_ReasonForInvalidFile = BADFORMAT;
					}
				}
			}
			else {
				// code to create larger read buffer
				m_BinaryFileBuffer = new char[FILE_BUFFER_SIZE];
				if (m_BinaryFileBuffer) {
					m_LASFile.Open(szFileName, m_BinaryFileBuffer, FILE_BUFFER_SIZE);
					if (m_LASFile.IsValid()) {
						m_HaveLocalBuffer = TRUE;
					}
					else {
						delete [] m_BinaryFileBuffer;
						m_HaveLocalBuffer = FALSE;

						// reopen file without the buffer
						m_LASFile.Open(szFileName);
					}
				}
				else {
					m_LASFile.Open(szFileName);
				}

				if (m_LASFile.IsValid()) {
					m_Version = (float) m_LASFile.Header.VersionMajor + (float) m_LASFile.Header.VersionMinor / 10.0f;
					m_LASFile.JumpToPointRecord(0);		// first point, index == 0
				}
				else {
					m_FileFormat = INVALID;
					m_ReasonForInvalidFile = BADFORMAT;
				}
			}
#endif
		}
		if (m_FileFormat != INVALID)
			m_Valid = TRUE;
		else
			m_Valid = FALSE;

		// get rid of expanded buffer
		if (!m_Valid) {
			if (m_HaveLocalBuffer) {
				m_HaveLocalBuffer = FALSE;
				delete [] m_BinaryFileBuffer;
				m_BinaryFileBuffer = NULL;
			}
		}
		else {
			// save file name
			m_FileName = szFileName;
		}
	}

	return(m_Valid);
}

void CLidarData::Rewind()
{
	if (m_Valid) {
		if (m_FileFormat == ASCIIDATA) {
			if (m_ASCIIFile.IsValid()) {
				// rewind
				m_ASCIIFile.Rewind();

				// read first line
				m_ASCIIFile.ReadASCIILine(m_LineBuffer);
			}
		}
		else if (m_FileFormat == BINDATA) {
			if (m_BinaryFile) 
				fseek(m_BinaryFile, 16, SEEK_SET);
		}
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			lasreader->seek(0);		// first point, index == 0
#else
			if (m_HaveLASZIP_DLL) {
				laszip_seek_point(lasdll_reader, 0);
			}
			else {
				m_LASFile.JumpToPointRecord(0);		// first point, index == 0
			}
#endif
		}
	}
}

BOOL CLidarData::ReadNextRecord(LIDARRETURN *ldat, LAS_RETURNFIELDS* LASdat)
{
	static double ptelev;

	if (m_Valid) {
		if (m_FileFormat == ASCIIDATA) {
			if (m_ASCIIFile.IsValid()) {
				if (m_ASCIIFile.ReadDataLine(m_LineBuffer, SKIPCOMMENTS)) {
					// parse values into structure
					if (m_Version == 1.0f) {
						if (sscanf(m_LineBuffer, "%i %i %lf %lf %f %f %f", &ldat->PulseNumber, &ldat->ReturnNumber, &ldat->X, &ldat->Y, &ldat->Elevation, &ldat->NadirAngle, &ldat->Intensity) == 7)
							return(TRUE);
					}
					else if (m_Version == 0.9f) {
						ldat->ReturnNumber = 0;
						if (sscanf(m_LineBuffer, "%i %lf %lf %f %f %f", &ldat->PulseNumber, &ldat->X, &ldat->Y, &ldat->Elevation, &ldat->NadirAngle, &ldat->Intensity) == 6)
							return(TRUE);
					}
					else 
						return(FALSE);
				}
			}
		}
		else if (m_FileFormat == BINDATA) {
			if (m_BinaryFile) {
				if (fread(ldat, sizeof(LIDARRETURN), 1, m_BinaryFile) == 1) {
					return(TRUE);
				}
			}
		}
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			if (lasreader != 0) {
				if (lasreader->read_point()) {
					ldat->X = lasreader->point.get_x();
					ldat->Y = lasreader->point.get_y();
					ptelev =  lasreader->point.get_z();
					ldat->Elevation = (float) ptelev;
//					ldat->PulseNumber = (int) (fmod(m_LASFile.PointRecord.GPSTime, 1.0) * 10000.0);
					ldat->PulseNumber = (int) (lasreader->point.gps_time);
					ldat->ReturnNumber = (int) lasreader->point.return_number;
					ldat->Intensity = (float) lasreader->point.intensity;
					ldat->NadirAngle = (float) lasreader->point.scan_angle_rank;

					if (LASdat) {
						LASdat->NumberOfReturns = lasreader->point.number_of_returns;
						LASdat->ScanDirectionFlag = lasreader->point.scan_direction_flag;
						LASdat->EdgeOfFlightLineFlag = lasreader->point.edge_of_flight_line;
						LASdat->V11Classification = lasreader->point.classification;
//						LASdat->V11Synthetic = m_LASFile.PointRecord.V11Synthetic;
//						LASdat->V11KeyPoint = m_LASFile.PointRecord.V11KeyPoint;
//						LASdat->V11Withheld = m_LASFile.PointRecord.V11Withheld;
						LASdat->UserData = lasreader->point.user_data;
						LASdat->PointSourceID = lasreader->point.point_source_ID;
						LASdat->GPSTime = lasreader->point.gps_time;
					}

					return(TRUE);
				}
			}
#else
			if (m_HaveLASZIP_DLL) {
				// check to see if there are points left in the file...laszip_get_point_count() returns the count of points
				// read from the file. Look to see if it is less than the total number of points in the file. Doing this
				// because I don't know how laszip.dll handles point reading and if it returns an error immediately when you
				// try to read more points than are contained in a file.
				__int64 pt64;
				laszip_get_point_count(lasdll_reader, &pt64);
				if (pt64 < lasdll_header->number_of_point_records) {
					if (!laszip_read_point(lasdll_reader)) {
						ldat->X = ((double) lasdll_point->X * lasdll_header->x_scale_factor) + lasdll_header->x_offset;
						ldat->Y = ((double) lasdll_point->Y * lasdll_header->y_scale_factor) + lasdll_header->y_offset;
						ptelev =  ((double) lasdll_point->Z * lasdll_header->z_scale_factor) + lasdll_header->z_offset;
						ldat->Elevation = (float) ptelev;
						ldat->PulseNumber = (int) (lasdll_point->gps_time);
						ldat->ReturnNumber = (int) lasdll_point->return_number;
						ldat->Intensity = (float) lasdll_point->intensity;
						ldat->NadirAngle = (float) lasdll_point->scan_angle_rank;

						// read the extended return number and scan angle
						if (lasdll_header->point_data_format > 5) {		// version specific interpretation of the classification flags must be done in code...not done in laszip dll
							ldat->ReturnNumber = lasdll_point->extended_return_number;
							ldat->NadirAngle = lasdll_point->extended_scan_angle;
						}

						if (LASdat) {
							LASdat->NumberOfReturns = lasdll_point->number_of_returns;
							LASdat->ScanDirectionFlag = lasdll_point->scan_direction_flag;
							LASdat->EdgeOfFlightLineFlag = lasdll_point->edge_of_flight_line;
							if (lasdll_header->point_data_format > 5) {		// version specific interpretation of the classification flags must be done in code...not done in laszip dll
								LASdat->V11Classification = lasdll_point->extended_classification;
								LASdat->V11Synthetic = lasdll_point->extended_classification_flags & 0x01;
								LASdat->V11KeyPoint = (lasdll_point->extended_classification_flags & 0x02) >> 1;
								LASdat->V11Withheld = (lasdll_point->extended_classification_flags & 0x04) >> 2;
								LASdat->V14Overlap = (lasdll_point->extended_classification_flags & 0x08) >> 3;

								// read the extended returns/pulse
								LASdat->NumberOfReturns = lasdll_point->extended_number_of_returns;
							}
							else {
								LASdat->V11Classification = lasdll_point->classification;
								LASdat->V11Synthetic = lasdll_point->synthetic_flag;
								LASdat->V11KeyPoint = lasdll_point->keypoint_flag;
								LASdat->V11Withheld = lasdll_point->withheld_flag;
								LASdat->V14Overlap = 0;
							}
							LASdat->UserData = lasdll_point->user_data;
							LASdat->PointSourceID = lasdll_point->point_source_ID;
							LASdat->GPSTime = lasdll_point->gps_time;
						}

						return(TRUE);
					}
					else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: reading point\n");
#endif
					}
				}
				else
					return(FALSE);
			}
			else {
				// version specific interpretation of the classification bit field is handled in ReadNextPoint()
				if (m_LASFile.IsValid()) {
					if (m_LASFile.ReadNextPoint()) {
						ldat->X = m_LASFile.PointRecord.X;
						ldat->Y = m_LASFile.PointRecord.Y;
						ldat->Elevation = (float) m_LASFile.PointRecord.Z;
	//					ldat->PulseNumber = (int) (fmod(m_LASFile.PointRecord.GPSTime, 1.0) * 10000.0);
						ldat->PulseNumber = (int) (m_LASFile.PointRecord.GPSTime);
						ldat->ReturnNumber = (int) m_LASFile.PointRecord.ReturnNumber;
						ldat->Intensity = (float) m_LASFile.PointRecord.Intensity;
						ldat->NadirAngle = (float) m_LASFile.PointRecord.ScanAngleRank;

						if (LASdat) {
							LASdat->NumberOfReturns = m_LASFile.PointRecord.NumberOfReturns;
							LASdat->ScanDirectionFlag = m_LASFile.PointRecord.ScanDirectionFlag;
							LASdat->EdgeOfFlightLineFlag = m_LASFile.PointRecord.EdgeOfFlightLineFlag;
							LASdat->V11Classification = m_LASFile.PointRecord.V11Classification;
							LASdat->V11Synthetic = m_LASFile.PointRecord.V11Synthetic;
							LASdat->V11KeyPoint = m_LASFile.PointRecord.V11KeyPoint;
							LASdat->V11Withheld = m_LASFile.PointRecord.V11Withheld;
							LASdat->V14Overlap = m_LASFile.PointRecord.V14Overlap;
							LASdat->UserData = m_LASFile.PointRecord.FileMarker;
							LASdat->PointSourceID = m_LASFile.PointRecord.UserBitField;
							LASdat->GPSTime = m_LASFile.PointRecord.GPSTime;
						}

						return(TRUE);
					}
				}
			}
#endif
		}
	}

	return(FALSE);
}

void CLidarData::Close()
{
	if (m_Valid) {
		if (m_FileFormat == ASCIIDATA) {
			if (m_ASCIIFile.IsValid()) {
				// close data file
				m_ASCIIFile.Close();
			}
		}
		else if (m_FileFormat == BINDATA) {
			if (m_BinaryFile) 
				fclose(m_BinaryFile);
		}
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			lasreader->close();
			delete lasreader;
#else
			if (m_HaveLASZIP_DLL) {
				if (laszip_close_reader(lasdll_reader)) {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: can't close reader\n");
#endif
				}
				else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: closed reader\n");
#endif
				}

				if (laszip_destroy(lasdll_reader)) {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL ERROR: can't destroy reader\n");
#endif
				}
				else {
#ifdef DLL_DIAGNOSTIC
fprintf(stderr,"DLL SUCCESS: destroyed reader\n");
#endif
				}
			}
			else {
				m_LASFile.Close();
			}
#endif
		}

		lasdll_reader = NULL;
		lasdll_header = NULL;
		lasdll_point = NULL;

		// delete local buffer
		if (m_HaveLocalBuffer) {
			m_HaveLocalBuffer = FALSE;
			delete [] m_BinaryFileBuffer;
			m_BinaryFileBuffer = NULL;
		}

		// clear COPC index
		if (m_IsCOPCFormat)
			m_COPCIndex.Destroy();
	}
	m_Valid = FALSE;
	m_ReasonForInvalidFile = NOFILEERROR;
	m_IsCompressed = FALSE;
	m_IsCOPCFormat = FALSE;
	m_COPCPointIndex = -1;
	m_COPCCurrentChunk = -1;
	m_FileName.Empty();
}

BOOL CLidarData::SetPosition(long Offset, int FromWhere)
{
	if (m_Valid) {
		if (m_FileFormat == ASCIIDATA)
			return(m_ASCIIFile.SetPosition(Offset, FromWhere));
		else if (m_FileFormat == BINDATA)
			return(fseek(m_BinaryFile, Offset, FromWhere) == 0);
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			// so long as the offset is from the beginning of a file, compute the point corresponding to the byte offset
			if (FromWhere != SEEK_SET)
				return(FALSE);

			__int64 pt;
			pt = (Offset - lasreader->header.offset_to_point_data) / lasreader->header.point_data_record_length;
			return(lasreader->seek(pt));
#else
			if (m_HaveLASZIP_DLL) {
				// so long as the offset is from the beginning of a file, compute the point corresponding to the byte offset
				if (FromWhere != SEEK_SET)
					return(FALSE);

				__int64 pt;
				pt = (Offset - lasdll_header->offset_to_point_data) / lasdll_header->point_data_record_length;
				return(laszip_seek_point(lasdll_reader, pt));
			}
			else {
				return(fseek(m_LASFile.m_FileHandle, Offset, FromWhere) == 0);
			}
#endif
		}
	}
	return(FALSE);
}

long CLidarData::GetPosition()
{
	if (m_Valid) {
		if (m_FileFormat == ASCIIDATA)
			return(m_ASCIIFile.GetPosition());
		else if (m_FileFormat == BINDATA)
			return(ftell(m_BinaryFile));
		else if (m_FileFormat == LASDATA) {
#ifdef USE_LASLIB
			// compute the byte offset given the current point number (p_count) and the offset to the start of the point data from the LAS header
			long offset;
			offset = lasreader->header.offset_to_point_data + (lasreader->p_count * lasreader->header.point_data_record_length);
			return(offset);
#else
			if (m_HaveLASZIP_DLL) {
				// compute the byte offset given the current point number (p_count) and the offset to the start of the point data from the LAS header
				long offset;
				__int64 offset64;
				__int64 pt64;
				laszip_get_point_count(lasdll_reader, &pt64);
				offset64 = (__int64) lasdll_header->offset_to_point_data + (pt64 * (__int64) lasdll_header->point_data_record_length);

				// truncate to 4 byte integer value...will not work for large files
				offset = (long) offset64;
				return(offset);

			}
			else {
				return(ftell(m_LASFile.m_FileHandle));
			}
#endif
		}
	}
	return(-1);
}

BOOL CLidarData::ConvertToBinary(LPCTSTR OutputFileName, BOOL CreateIndex, BOOL DecimateByPulse)
{
	LIDARRETURN pt;
	char signature[9] = { "LIDARBIN" };
//	char* signature = { "LIDARBIN" };
	int major = 1;
	int minor = 1;

	double minx = DBL_MAX;
	double miny = DBL_MAX;
	double minz = DBL_MAX;
	double maxx = -DBL_MAX;
	double maxy = -DBL_MAX;
	double maxz = -DBL_MAX;

	int ptcnt = 0;

	if (!m_Valid)
		return(FALSE);

	if (m_FileFormat == BINDATA && !CreateIndex)
		return(TRUE);

	// variables for pulse decimation
	int PulseCount = 0;
	double LastGPSTime = 0.0;
	BOOL ScanAngleIncreasing = FALSE;
	int LastScanAngle = -1000;

	FILE* f = fopen(OutputFileName, "wb");
	if (f) {
		Rewind();

		// Write new header
		fwrite(signature, sizeof(char), 8, f);
		fwrite(&major, sizeof(int), 1, f);
		fwrite(&minor, sizeof(int), 1, f);

		while (ReadNextRecord(&pt)) {
			if (LastPointIsWithheld())
				continue;

			if (m_FileFormat == LASDATA && DecimateByPulse) {
#ifdef USE_LASLIB
				if (lasreader->point.gps_time != LastGPSTime) {
					PulseCount ++;
					LastGPSTime = lasreader->point.gps_time;
				}

				// look at scan angle geometry
				if (ScanAngleIncreasing && lasreader->point.scan_angle_rank < LastScanAngle)
					ScanAngleIncreasing = FALSE;
				else if (lasreader->point.scan_angle_rank > LastScanAngle)
					ScanAngleIncreasing = TRUE;

				LastScanAngle = lasreader->point.scan_angle_rank;
#else
				if (m_HaveLASZIP_DLL) {
					if (lasdll_point->gps_time != LastGPSTime) {
						PulseCount ++;
						LastGPSTime = lasdll_point->gps_time;
					}

					// look at scan angle geometry
					if (ScanAngleIncreasing && lasdll_point->scan_angle_rank < LastScanAngle)
						ScanAngleIncreasing = FALSE;
					else if (lasdll_point->scan_angle_rank > LastScanAngle)
						ScanAngleIncreasing = TRUE;

					LastScanAngle = lasdll_point->scan_angle_rank;
				}
				else {
					if (m_LASFile.PointRecord.GPSTime != LastGPSTime) {
						PulseCount ++;
						LastGPSTime = m_LASFile.PointRecord.GPSTime;
					}

					// look at scan angle geometry
					if (ScanAngleIncreasing && m_LASFile.PointRecord.ScanAngleRank < LastScanAngle)
						ScanAngleIncreasing = FALSE;
					else if (m_LASFile.PointRecord.ScanAngleRank > LastScanAngle)
						ScanAngleIncreasing = TRUE;

					LastScanAngle = m_LASFile.PointRecord.ScanAngleRank;	
				}
#endif

				if (PulseCount % 2 != 0)
					continue;

				if (!ScanAngleIncreasing)
					continue;
			}

			fwrite(&pt, sizeof(LIDARRETURN), 1, f);

			if (CreateIndex) {
				minx = __min(minx, pt.X);
				miny = __min(miny, pt.Y);
				minz = __min(minz, pt.Elevation);
				maxx = __max(maxx, pt.X);
				maxy = __max(maxy, pt.Y);
				maxz = __max(maxz, pt.Elevation);
			}
			ptcnt ++;
		}
		fclose(f);

		if (CreateIndex) {
			CDataIndex index;
			index.CreateIndex(OutputFileName, 10, 256, 256, ptcnt, minx, maxx, miny, maxy, minz, maxz, TRUE);
		}
		return(TRUE);
	}

	return(FALSE);
}

BOOL CLidarData::ConvertToASCII(LPCTSTR OutputFileName, int Format, int StartRecord, int NumberOfRecords)
{
	LIDARRETURN pt;
	LAS_RETURNFIELDS LASdat;

	if (!m_Valid)
		return(FALSE);

	FILE* f = fopen(OutputFileName, "wt");
	if (f) {
		Rewind();

		// Write header
		if (Format == 2)
			fprintf(f, ";PT#, X, Y, Elevation\n");
		else if (Format == 1)
			fprintf(f, "\"X\",\"Y\",\"Elevation\",\"Intensity\"\n");
		else if (Format == 0)		// default format
			fprintf(f, "\"X\",\"Y\",\"Elevation\",\"Intensity\",\"PulseNumber\",\"ReturnNumber\",\"NadirAngle\"\n");
		else if (Format == 3)		// point number plus default
			fprintf(f, "\"PtNumber\",\"X\",\"Y\",\"Elevation\",\"Intensity\",\"PulseNumber\",\"ReturnNumber\",\"NadirAngle\"\n");
		else if (Format == 4 && m_FileFormat == LASDATA)			// LAS everything
			fprintf(f, "\"Point number\",\"GPS time\",\"X\",\"Y\",\"Elevation\",\"Intensity\",\"Return number\",\"Number of returns\",\"Scan direction flag\",\"Flightline edge\",\"Classification\",\"V1.1 Synthetic pt\",\"V1.1 Key-point\",\"V1.1 Withheld\",\"Scan angle rank\",\"User data\",\"Point source ID\"\n");
		else if (Format == 5)		// all LDA fields...tab delimted
			fprintf(f, "\"X\"\t\"Y\"\t\"Elevation\"\t\"Intensity\"\t\"PulseNumber\"\t\"ReturnNumber\"\t\"NadirAngle\"\n");

		int cnt = 0;
//		int specialcnt = 0;
		while (ReadNextRecord(&pt, &LASdat)) {
			if (LastPointIsWithheld())
				continue;

			if (cnt >= StartRecord) {
				// write point
				if (Format == 2)
					fprintf(f, "%i,%lf,%lf,%f\n", cnt + 1, pt.X, pt.Y, pt.Elevation);
				else if (Format == 1)
					fprintf(f, "%lf,%lf,%f,%f\n", pt.X, pt.Y, pt.Elevation, pt.Intensity);
				else if (Format == 0)		// default format
					fprintf(f, "%lf,%lf,%f,%f,%i,%i,%f\n", pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.PulseNumber, pt.ReturnNumber, pt.NadirAngle);
				else if (Format == 3)		// point number plus default format
					fprintf(f, "%i,%lf,%lf,%f,%f,%i,%i,%f\n", cnt + 1, pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.PulseNumber, pt.ReturnNumber, pt.NadirAngle);
				else if (Format == 4 && m_FileFormat == LASDATA) {		// LAS dump
#ifdef USE_LASLIB
					fprintf(f, "%i,%lf,%lf,%lf,%f,%f,%i,%i,%i,%i,%i,%i,%i,%i,%f,%i,%i\n", cnt + 1, lasreader->point.gps_time, pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.ReturnNumber, LASdat.NumberOfReturns, LASdat.ScanDirectionFlag, LASdat.EdgeOfFlightLineFlag, LASdat.V11Classification, LASdat.V11Synthetic, LASdat.V11KeyPoint, LASdat.V11Withheld, pt.NadirAngle, LASdat.UserData, LASdat.PointSourceID);
#else
					if (m_HaveLASZIP_DLL) {
						fprintf(f, "%i,%lf,%lf,%lf,%f,%f,%i,%i,%i,%i,%i,%i,%i,%i,%f,%i,%i\n", cnt + 1, lasdll_point->gps_time, pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.ReturnNumber, LASdat.NumberOfReturns, LASdat.ScanDirectionFlag, LASdat.EdgeOfFlightLineFlag, LASdat.V11Classification, LASdat.V11Synthetic, LASdat.V11KeyPoint, LASdat.V11Withheld, pt.NadirAngle, LASdat.UserData, LASdat.PointSourceID);
					}
					else {
						fprintf(f, "%i,%lf,%lf,%lf,%f,%f,%i,%i,%i,%i,%i,%i,%i,%i,%f,%i,%i\n", cnt + 1, m_LASFile.PointRecord.GPSTime, pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.ReturnNumber, LASdat.NumberOfReturns, LASdat.ScanDirectionFlag, LASdat.EdgeOfFlightLineFlag, LASdat.V11Classification, LASdat.V11Synthetic, LASdat.V11KeyPoint, LASdat.V11Withheld, pt.NadirAngle, LASdat.UserData, LASdat.PointSourceID);
					}
#endif
				}
				else if (Format == 5)		// all LDA fields...tab delimited
					fprintf(f, "%lf\t%lf\t%f\t%f\t%i\t%i\t%f\n", pt.X, pt.Y, pt.Elevation, pt.Intensity, pt.PulseNumber, pt.ReturnNumber, pt.NadirAngle);
			}

			cnt ++;

			if (NumberOfRecords > 0 && cnt >= (StartRecord + NumberOfRecords))
				break;
		}
		fclose(f);

		return(TRUE);
	}

	return(FALSE);
}

#ifndef USE_LASLIB
BOOL CLidarData::ConvertToLAS(LPCTSTR OutputFileName, BOOL CreateIndex, BOOL PreserveLASInfo, BOOL CleanLASPoints, BOOL OutputLASClasses, int MaxNumberOfLASClasses, int* LASClassFlags, BOOL SingleLine, int LineNumber, BOOL TimeSlice, double StartTime, double StopTime, BOOL CountSlice, unsigned __int64 StartCount, unsigned __int64 StopCount)
{
	long lxMax = LONG_MIN;
	long lxMin = LONG_MAX;
	long lyMax = LONG_MIN;
	long lyMin = LONG_MAX;
	long lzMax = LONG_MIN;
	long lzMin = LONG_MAX;

	bool NeedCompatibilityMode = false;

	// input file is already opened so we just need to create and write a header, then read points and write them
	if (m_HaveLASZIP_DLL) {
		LIDARRETURN pt;
		laszip_U32 PointsToRead = 0;

		// create the writer
		laszip_POINTER laszip_writer;
		if (laszip_create(&laszip_writer)) {
			return(FALSE);
		}

		// get a pointer to the header of the writer so we can populate it
		laszip_header* header_write;

		if (laszip_get_header_pointer(laszip_writer, &header_write)) {
			// destroy the writer
			laszip_destroy(laszip_writer);
			return(FALSE);
		}

		// copy entries from the reader header to the writer header
		laszip_U32 i;

		if (m_FileFormat == LASDATA) {
			// "remove" EVLR...will be appended to output file at the end
			header_write->number_of_extended_variable_length_records = 0;
			header_write->start_of_first_extended_variable_length_record = 0;

			// copy input file header to output header
		    laszip_set_header(laszip_writer, lasdll_header);

			// check for COPC VLR and remove
			if (header_write->number_of_variable_length_records) {
				if (strncmp(header_write->vlrs[0].user_id, "copc", 16) == 0 && header_write->vlrs[0].record_id == 1) {
					laszip_remove_vlr(header_write, "copc\0\0\0\0\0\0\0\0\0\0\0\0", 1);
				}
			}

			// comment on next line is confusing. NeedCompatibilityMode = true is ignored farther down in the code and compatibility mode is never used
			if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4)		// as of 2/26/2018 changed to use native 1.4 compression instead of compatability mode compression
				NeedCompatibilityMode = true;

			PointsToRead = header_write->number_of_point_records;
		}
		else {
			// count returns and get min/max XYZ
			Rewind();
			header_write->number_of_point_records = 0;
			for (i = 0; i < 5; i++) {
				header_write->number_of_points_by_return[i] = 0;
			}

			while (ReadNextRecord(&pt)) {
				PointsToRead ++;

				if (LastPointIsWithheld())
					continue;

				if (SingleLine && lasdll_point->point_source_ID != LineNumber)
					continue;

				if (TimeSlice && (lasdll_point->gps_time < StartTime || lasdll_point->gps_time >= StopTime))
					continue;

				if (CountSlice && (PointsToRead < StartCount || PointsToRead > StopCount))
					continue;

				if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 5) {
					header_write->min_x = __min(header_write->min_x, pt.X);
					header_write->min_y = __min(header_write->min_y, pt.Y);
					header_write->min_z = __min(header_write->min_z, pt.Elevation);
					header_write->max_x = __max(header_write->max_x, pt.X);
					header_write->max_y = __max(header_write->max_y, pt.Y);
					header_write->max_z = __max(header_write->max_z, pt.Elevation);

					header_write->number_of_points_by_return[pt.ReturnNumber - 1] ++;

					header_write->number_of_point_records ++;
				}
			}

			if (!header_write->number_of_point_records) {
				// destroy the writer
				laszip_destroy(laszip_writer);
				return(FALSE);
			}

			// set up scaling
			header_write->x_scale_factor = 1.0 / 1000.0;
			header_write->y_scale_factor = 1.0 / 1000.0;
			header_write->z_scale_factor = 1.0 / 1000.0;
			header_write->x_offset = header_write->min_x;
			header_write->y_offset = header_write->min_y;
			header_write->z_offset = header_write->min_z;

			// set generating software
//			strcpy(header_write->generating_software, "USDA-FS FUSION/LDV");
			strcpy(header_write->generating_software, "FUSION ConvertToLAS()");
			header_write->file_source_ID = 0;
			header_write->global_encoding = 0;
			header_write->project_ID_GUID_data_1 = 0;
			header_write->project_ID_GUID_data_2 = 0;
			header_write->project_ID_GUID_data_3 = 0;
			memset(header_write->project_ID_GUID_data_4, 0, 8);
			header_write->version_major = 1;
			header_write->version_minor = 0;
			memset(header_write->system_identifier, 0, 32);
			memset(header_write->generating_software, 0, 32);
			strcpy(header_write->generating_software, "FUSION LAS Library");
			header_write->file_creation_day = 0;
			header_write->file_creation_year = 0;
			header_write->header_size = 227;
			header_write->offset_to_point_data = 227;
			header_write->number_of_variable_length_records = 0;
			header_write->point_data_format = 0;
			header_write->point_data_record_length = 20;

			Rewind();
		}

		// open the writer
		// copy the output file name and make it lower case to test for LAZ output...capitalization will be preserved in the actual output file name
		CString csTemp = OutputFileName;
		csTemp.MakeLower();
		laszip_BOOL compress = (strstr(csTemp, ".laz") != 0);

		// 1/18/2017 code to enable V1.4 compatibility mode for compressed output
		// as of 2/26/2018 changed to use native 1.4 compression instead of compatability mode compression
		// compatibility mode is never used
		if (compress && NeedCompatibilityMode) {
			laszip_BOOL request = 1;
			if (laszip_request_native_extension(laszip_writer, request)) {
//			if (laszip_request_compatibility_mode(laszip_writer, request)) {
				// destroy the writer
				laszip_destroy(laszip_writer);
				return(FALSE);
			}
		}
		// 1/18/2017 end of code to enable V1.4 compatibility mode for compressed output

		// 2/2/2017 keep the identifying string related to FUSION tool
		laszip_preserve_generating_software(laszip_writer, true);

		if (laszip_open_writer(laszip_writer, OutputFileName, compress)) {
			laszip_CHAR* error;
			if (laszip_get_error(laszip_writer, &error)) {
				fprintf(stderr,"DLL ERROR: getting error messages\n");
			}
			fprintf(stderr,"DLL ERROR MESSAGE: %s\n", error);
			
			// destroy the writer
			laszip_destroy(laszip_writer);
			return(FALSE);
		}

		// get a pointer to the points of the reader will be read
		laszip_point* point_read;
		point_read = lasdll_point;

		// get a pointer to the points of the writer that we will populate and write
		laszip_point* point_write;

		if (laszip_get_point_pointer(laszip_writer, &point_write)) {
			// close the writer
			laszip_close_writer(laszip_writer);

			// destroy the writer
			laszip_destroy(laszip_writer);
			return(FALSE);
		}

		// read the points
		laszip_U32 Writtencount = 0;
		laszip_U32 Totalcount = 0;

		header_write->number_of_points_by_return[0] = 0;
		header_write->number_of_points_by_return[1] = 0;
		header_write->number_of_points_by_return[2] = 0;
		header_write->number_of_points_by_return[3] = 0;
		header_write->number_of_points_by_return[4] = 0;
		header_write->number_of_point_records = 0;

		// V1.4+
		for (i = 0; i < 15; i++) {
			header_write->extended_number_of_points_by_return[i] = 0;
		}
		header_write->extended_number_of_point_records = 0;

//		while (Totalcount < lasdll_header->number_of_point_records) {
		while (Totalcount < PointsToRead) {
			// read a point
			if (!ReadNextRecord(&pt)) {
				// close the writer
				laszip_close_writer(laszip_writer);

				// destroy the writer
				laszip_destroy(laszip_writer);
				return(FALSE);
			}

			Totalcount ++;

			if (LastPointIsWithheld())
				continue;

			// copy the point
			if (m_FileFormat == LASDATA) {
				if (SingleLine && lasdll_point->point_source_ID != LineNumber)
					continue;

				if (TimeSlice && (lasdll_point->gps_time < StartTime || lasdll_point->gps_time >= StopTime))
					continue;

				if (CountSlice && (Totalcount < StartCount || Totalcount > StopCount))
					continue;

				// check to see if we are only outputting specific LAS classes
				if (OutputLASClasses) {
					if (lasdll_point->classification >= 0 && lasdll_point->classification < MaxNumberOfLASClasses) {
						if (LASClassFlags[lasdll_point->classification] == 0) {
							continue;
						}
					}
				}

				// check point for valid LAS info
				if (CleanLASPoints) {
					// check for points outside header extent
					if (pt.X < lasdll_header->min_x - 0.25 * lasdll_header->x_scale_factor || pt.X > lasdll_header->max_x + 0.25 * lasdll_header->x_scale_factor ||
						pt.Y < lasdll_header->min_y - 0.25 * lasdll_header->y_scale_factor || pt.Y > lasdll_header->max_y + 0.25 * lasdll_header->y_scale_factor ||
						pt.Elevation < lasdll_header->min_z - 0.25 * lasdll_header->z_scale_factor || pt.Elevation > lasdll_header->max_z + 0.25 * lasdll_header->z_scale_factor)
						continue;

					// check for withheld points
					if (LastPointIsWithheld())
						continue;

					// check GPS time
					if ((lasdll_header->global_encoding & 1) == 0 && (lasdll_header->point_data_format == 1 || lasdll_header->point_data_format == 3 || lasdll_header->point_data_format >= 4)) {
						if (lasdll_point->gps_time < 0.0 || lasdll_point->gps_time > 604800.0) {
							continue;
						}
					}

					// check return number...must be 1-5 for versions prior to 1.4, 1-15 for version 1.4
					if (lasdll_header->version_major == 1 && lasdll_header->version_minor < 4) {
						if (pt.ReturnNumber <= 0 || pt.ReturnNumber > 5)
							continue;
					}
					else if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4) {
						if (pt.ReturnNumber <= 0 || pt.ReturnNumber > 15)
							continue;
					}
				}

				point_write->X = lasdll_point->X;
				point_write->Y = lasdll_point->Y;
				point_write->Z = lasdll_point->Z;
				point_write->intensity = lasdll_point->intensity;
				point_write->return_number = lasdll_point->return_number;
				point_write->number_of_returns = lasdll_point->number_of_returns;
				point_write->scan_direction_flag = lasdll_point->scan_direction_flag;
				point_write->edge_of_flight_line = lasdll_point->edge_of_flight_line;
				point_write->classification = lasdll_point->classification;
				point_write->scan_angle_rank = lasdll_point->scan_angle_rank;
				point_write->user_data = lasdll_point->user_data;
				point_write->point_source_ID = lasdll_point->point_source_ID;

				point_write->gps_time = lasdll_point->gps_time;

				memcpy(point_write->rgb, lasdll_point->rgb, 8);
				memcpy(point_write->wave_packet, lasdll_point->wave_packet, 29);

				// LAS 1.4 only
				point_write->extended_point_type = lasdll_point->extended_point_type;
				point_write->extended_scanner_channel = lasdll_point->extended_scanner_channel;
				point_write->extended_classification_flags = lasdll_point->extended_classification_flags;
				point_write->extended_classification = lasdll_point->extended_classification;
				point_write->extended_return_number = lasdll_point->extended_return_number;
				point_write->extended_number_of_returns = lasdll_point->extended_number_of_returns;
				point_write->extended_scan_angle = lasdll_point->extended_scan_angle;

				// 5/31/2024
				// for 1.4 files and point formats 6+, make sure legacy point classification fields match extended_classification_flags
				// code in laszip_dll.cpp tests for equality and fails if they are not equal. However, the test uses intensity incorrectly 
				// as a 4 byte value: ((((U8)&(point_write->intensity))[3]) >> 5) and references bytes that are not part of intensity.
				// Fortunately, the laszip_point structure has the bitfields for the flags immediately after intensity so the offset
				// to the third byte access the correct location in memory.
				if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4 && lasdll_header->point_data_format >= 6) {
					point_write->synthetic_flag = point_write->extended_classification_flags & 0x1;
					point_write->keypoint_flag = (point_write->extended_classification_flags & 0x2) >> 1;
					point_write->withheld_flag = (point_write->extended_classification_flags & 0x4) >> 2;
				}

				if (lasdll_point->num_extra_bytes) {
					memcpy(point_write->extra_bytes, lasdll_point->extra_bytes, lasdll_point->num_extra_bytes);
				}

				// update the extent and the counts by return
				// compare point to raw XYZ values to adjust data extent
				lxMin = min(point_write->X, lxMin);
				lyMin = min(point_write->Y, lyMin);
				lzMin = min(point_write->Z, lzMin);
				lxMax = max(point_write->X, lxMax);
				lyMax = max(point_write->Y, lyMax);
				lzMax = max(point_write->Z, lzMax);

				if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 5)
					header_write->number_of_points_by_return[pt.ReturnNumber - 1] ++;

				header_write->number_of_point_records ++;

				// V1.4+
				if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 15)
					header_write->extended_number_of_points_by_return[pt.ReturnNumber - 1] ++;

				header_write->extended_number_of_point_records ++;
			}
			else {
				// output will be las V1.0 and won't have all the fields
				point_write->X = (laszip_I32) ((pt.X - header_write->x_offset) / header_write->x_scale_factor);
				point_write->Y = (laszip_I32) ((pt.Y - header_write->y_offset) / header_write->y_scale_factor);
				point_write->Z = (laszip_I32) ((pt.Elevation - header_write->z_offset) / header_write->z_scale_factor);
				point_write->intensity = (laszip_U16) pt.Intensity;
				point_write->return_number = pt.ReturnNumber;
				point_write->number_of_returns = 0;
				point_write->scan_direction_flag = 0;
				point_write->edge_of_flight_line = 0;
				point_write->classification = 0;
				point_write->scan_angle_rank = 0;
				point_write->user_data = 0;
				point_write->point_source_ID = 0;

				point_write->gps_time = 0;

				// compare point to raw XYZ values to adjust data extent
				lxMin = min(point_write->X, lxMin);
				lyMin = min(point_write->Y, lyMin);
				lzMin = min(point_write->Z, lzMin);
				lxMax = max(point_write->X, lxMax);
				lyMax = max(point_write->Y, lyMax);
				lzMax = max(point_write->Z, lzMax);

				if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 5)
					header_write->number_of_points_by_return[pt.ReturnNumber - 1] ++;

				header_write->number_of_point_records ++;
			}

			// write the point
			if (laszip_write_point(laszip_writer)) {
				laszip_CHAR* error;
				if (laszip_get_error(laszip_writer, &error))
				{
					fprintf(stderr, "DLL ERROR: getting error messages\n");
				}
				fprintf(stderr, "DLL ERROR MESSAGE: %s\n", error);

				// close the writer
				laszip_close_writer(laszip_writer);

				// destroy the writer
				laszip_destroy(laszip_writer);
				return(FALSE);
			}

			Writtencount++;
		}

		// adjust the extent
		header_write->min_x = ((double) lxMin * header_write->x_scale_factor) + header_write->x_offset;
		header_write->min_y = ((double) lyMin * header_write->y_scale_factor) + header_write->y_offset;
		header_write->min_z = ((double) lzMin * header_write->z_scale_factor) + header_write->z_offset;
		header_write->max_x = ((double) lxMax * header_write->x_scale_factor) + header_write->x_offset;
		header_write->max_y = ((double) lyMax * header_write->y_scale_factor) + header_write->y_offset;
		header_write->max_z = ((double) lzMax * header_write->z_scale_factor) + header_write->z_offset;

		// close the writer
		if (laszip_close_writer(laszip_writer)) {
			// destroy the writer
			laszip_destroy(laszip_writer);
			return(FALSE);
		}

		// update the file header...different functions for V1.4+ and V1.0-1.3
		if (header_write->version_major == 1 && header_write->version_minor >= 4) {
			UpdateLASHeader(OutputFileName, TRUE, FALSE, TRUE, TRUE,
				header_write->min_x, header_write->min_y, header_write->min_z,
				header_write->max_x, header_write->max_y, header_write->max_z,
				header_write->x_offset, header_write->y_offset, header_write->z_offset,
				header_write->extended_number_of_points_by_return,
				header_write->extended_number_of_point_records);
		}
		else {
			UpdateLASHeader(OutputFileName, TRUE, FALSE, TRUE, TRUE, header_write->min_x, header_write->min_y, header_write->min_z, header_write->max_x, header_write->max_y, header_write->max_z,
				header_write->x_offset, header_write->y_offset, header_write->z_offset,
				header_write->number_of_points_by_return[0],
				header_write->number_of_points_by_return[1],
				header_write->number_of_points_by_return[2],
				header_write->number_of_points_by_return[3],
				header_write->number_of_points_by_return[4],
				header_write->number_of_point_records);
		}

		// copy EVLRs from first input file to output file
		if (header_write->version_major == 1 && header_write->version_minor >= 4) {
			if (IsValid()) {
				if (GetFileFormat() == LASDATA) {
					CopyEVLRs(m_FileName, OutputFileName);
				}
			}
		}

		// destroy the writer
		if (laszip_destroy(laszip_writer)) {
			return(FALSE);
		}

		// create index
		if (CreateIndex) {
			CDataIndex index;
			index.CreateIndex(OutputFileName, 10, 256, 256, header_write->number_of_point_records, header_write->min_x, header_write->max_x, header_write->min_y, header_write->max_y, header_write->min_z, header_write->max_z, TRUE);
		}

		return(TRUE);
	}
	else {
		// don't have the laszip.dll
		//
		LIDARRETURN pt;
		LAS_RETURNFIELDS LASFields;
		CLASPublicHeaderBlock LASHeader;
		CLASPointDataRecord LASPt;
		unsigned char startofdata[3] = {0xDD,0xCC};

		if (!m_Valid)
			return(FALSE);

		LASHeader.MinX = DBL_MAX;
		LASHeader.MinY = DBL_MAX;
		LASHeader.MinZ = DBL_MAX;
		LASHeader.MaxX = -DBL_MAX;
		LASHeader.MaxY = -DBL_MAX;
		LASHeader.MaxZ = -DBL_MAX;
		LASHeader.NumberOfPointRecords = 0;
		for (int i = 0; i < 15; i ++)
			LASHeader.NumberOfPointsByReturn[i] = 0;

		// count returns and get min/max XYZ
		Rewind();
		while (ReadNextRecord(&pt, &LASFields)) {
			if (LastPointIsWithheld())
				continue;

			if (m_FileFormat == LASDATA) {
				if (SingleLine && LASFields.PointSourceID != LineNumber)
					continue;

				if (TimeSlice && (LASFields.GPSTime < StartTime || LASFields.GPSTime >= StopTime))
					continue;
			}

			if (m_FileFormat == LASDATA && CleanLASPoints) {
				// check to see if we are only outputting specific LAS classes
				if (OutputLASClasses) {
					if (m_LASFile.PointRecord.Classification >= 0 && m_LASFile.PointRecord.Classification < MaxNumberOfLASClasses) {
						if (LASClassFlags[m_LASFile.PointRecord.Classification] == 0) {
							continue;
						}
					}
				}

				// check for points outside header extent
				if (pt.X < m_LASFile.Header.MinX - 0.25 * m_LASFile.Header.XScaleFactor || pt.X > m_LASFile.Header.MaxX + 0.25 * m_LASFile.Header.XScaleFactor ||
					pt.Y < m_LASFile.Header.MinY - 0.25 * m_LASFile.Header.YScaleFactor || pt.Y > m_LASFile.Header.MaxY + 0.25 * m_LASFile.Header.YScaleFactor ||
					pt.Elevation < m_LASFile.Header.MinZ - 0.25 * m_LASFile.Header.ZScaleFactor || pt.Elevation > m_LASFile.Header.MaxZ + 0.25 * m_LASFile.Header.ZScaleFactor)
					continue;

				// check for withheld points
				if (LastPointIsWithheld())
					continue;

				// check GPS time
				if ((m_LASFile.Header.Reserved.V11.Reserved & 1) == 0 && (m_LASFile.Header.PointDataFormatID == 1 || m_LASFile.Header.PointDataFormatID == 3 || m_LASFile.Header.PointDataFormatID >= 4)) {
					if (m_LASFile.PointRecord.GPSTime < 0.0 || m_LASFile.PointRecord.GPSTime > 604800.0) {
						continue;
					}
				}

				// check return number...must be 1-15
				if (pt.ReturnNumber <= 0 || pt.ReturnNumber > 15)
					continue;
			}

			if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 15) {
				LASHeader.MinX = __min(LASHeader.MinX, pt.X);
				LASHeader.MinY = __min(LASHeader.MinY, pt.Y);
				LASHeader.MinZ = __min(LASHeader.MinZ, pt.Elevation);
				LASHeader.MaxX = __max(LASHeader.MaxX, pt.X);
				LASHeader.MaxY = __max(LASHeader.MaxY, pt.Y);
				LASHeader.MaxZ = __max(LASHeader.MaxZ, pt.Elevation);

				LASHeader.NumberOfPointsByReturn[pt.ReturnNumber - 1] ++;

				LASHeader.NumberOfPointRecords ++;
			}
		}

		if (!LASHeader.NumberOfPointRecords)
			return(FALSE);

		// set up the header using the header for the input file...if not LAS format, create LAS 1.0 file
		if (m_FileFormat == LASDATA) {
			// populate the header for output files
			LASHeader.VersionMajor = m_LASFile.Header.VersionMajor;
			LASHeader.VersionMinor = m_LASFile.Header.VersionMinor;
			LASHeader.FlightDateJulian = m_LASFile.Header.FlightDateJulian;
			LASHeader.Year = m_LASFile.Header.Year;
			LASHeader.PointDataFormatID = m_LASFile.Header.PointDataFormatID;
			LASHeader.PointDataRecordLength = m_LASFile.Header.PointDataRecordLength;
			LASHeader.NumberOfVariableLengthRecords = m_LASFile.Header.NumberOfVariableLengthRecords;

			// new variables for V1.4...12/21/2016
			LASHeader.GUIDData1 = m_LASFile.Header.GUIDData1;
			LASHeader.GUIDData2 = m_LASFile.Header.GUIDData2;
			LASHeader.GUIDData3 = m_LASFile.Header.GUIDData3;
			memcpy(LASHeader.GUIDData4, m_LASFile.Header.GUIDData4, 8);
			LASHeader.Reserved.V11.FileSourceID = m_LASFile.Header.Reserved.V11.FileSourceID;
			LASHeader.Reserved.V11.Reserved = m_LASFile.Header.Reserved.V11.Reserved;
			LASHeader.HeaderSize = m_LASFile.Header.HeaderSize;
			LASHeader.OffsetToData = m_LASFile.Header.OffsetToData;

			if (LASHeader.VersionMajor == 1) {
				if (LASHeader.VersionMinor == 3) {
					// do format-specific fields
					LASHeader.WaveformStart = m_LASFile.Header.WaveformStart;
				}
				if (LASHeader.VersionMinor >= 4) {		// need to adjust logic when LAS v1.5 comes out
					// do format-specific fields
					LASHeader.WaveformStart = m_LASFile.Header.WaveformStart;
					LASHeader.StartOfExtendedVLR = m_LASFile.Header.StartOfExtendedVLR;
					LASHeader.NumberOfExtendedVLRs = m_LASFile.Header.NumberOfExtendedVLRs;
//								LASHeader.ExtendedNumberOfPointRecords = LASExtendedNumberOfPointRecords;
//								for (i = 0; i < 15; i ++)
//									LASHeader.ExtendedNumberOfPointsByReturn[i] = LASExtendedNumberOfPointsByReturn[i];
				}
			}
			else {
				// V2.0+
			}

			// see if there is extra data in the header
			if (m_LASFile.Header.UserDataInHeaderSize > 0) {
				LASHeader.UserDataInHeaderSize = m_LASFile.Header.UserDataInHeaderSize;

				// allocate memory for data and copy from header
				LASHeader.UserDataInHeader = new unsigned char[LASHeader.UserDataInHeaderSize];
				if (LASHeader.UserDataInHeader) {
					memcpy(LASHeader.UserDataInHeader, m_LASFile.Header.UserDataInHeader, LASHeader.UserDataInHeaderSize);
				}
				else {
					m_LASFile.Header.UserDataInHeaderSize = 0;
					m_LASFile.Header.UserDataInHeader = NULL;
				}
			}

			// see if there is extra data after the header
			if (m_LASFile.Header.UserDataAfterHeaderSize > 0) {
				LASHeader.UserDataAfterHeaderSize = m_LASFile.Header.UserDataAfterHeaderSize;

				// allocate memory for data and copy from header
				LASHeader.UserDataAfterHeader = new unsigned char[LASHeader.UserDataAfterHeaderSize];
				if (LASHeader.UserDataAfterHeader) {
					memcpy(LASHeader.UserDataAfterHeader, m_LASFile.Header.UserDataAfterHeader, LASHeader.UserDataAfterHeaderSize);
				}
				else {
					LASHeader.UserDataAfterHeaderSize = 0;
					LASHeader.UserDataAfterHeader = NULL;
				}
			}
		}

		// set up scaling
		LASHeader.XScaleFactor = m_LASFile.Header.XScaleFactor;
		LASHeader.YScaleFactor = m_LASFile.Header.YScaleFactor;
		LASHeader.ZScaleFactor = m_LASFile.Header.ZScaleFactor;
		LASHeader.XOffset = LASHeader.MinX;
		LASHeader.YOffset = LASHeader.MinY;
		LASHeader.ZOffset = LASHeader.MinZ;

		CString csTemp;

		// set generating software
//		strcpy(LASHeader.GeneratingSoftware, "USDA-FS FUSION/LDV");
//		strcpy(LASHeader.GeneratingSoftware, "FUSION ConvertToLAS()");
		csTemp.Format("FUSION v%.2lf ConvertToLAS()", FUSION_VERSION);		// watch length...depends on program name
		strcpy(LASHeader.GeneratingSoftware, csTemp);

		int LASVRLength;
		unsigned __int64 LASPointRecordCount = 0;
		unsigned __int64 LASReturnCounts[15];

		for (int i = 0; i < 15; i ++)
			LASReturnCounts[i] = 0;

		FILE* f = fopen(OutputFileName, "wb");
		if (f) {
			Rewind();

			// Write header
			LASHeader.Write(f);

			// modified code for V1.4...12/21/2016
			if (LASHeader.UserDataInHeaderSize > 0) {
				// write extra bytes in header...technically not allowed in 1.4
				fwrite(LASHeader.UserDataInHeader, sizeof(unsigned char), LASHeader.UserDataInHeaderSize, f);
			}
			// end of modified code for V1.4...12/21/2016

			fseek(f, LASHeader.HeaderSize, SEEK_SET);

			if (m_FileFormat == LASDATA) {
				// copy variable length records from first data file...must be LAS or else we can't get to this point in the code
				if (m_LASFile.Header.NumberOfVariableLengthRecords) {
					for (unsigned long k = 0; k < m_LASFile.Header.NumberOfVariableLengthRecords; k ++) {
						LASVRLength = m_LASFile.CopyVariableRecord(k, f);
						if (LASVRLength < 0) {
							// error
							csTemp.Format("Problems copying variable length records from LAS file: %s", (LPCTSTR) m_FileName);
//							LTKCL_PrintStatus(csTemp);
						}
					}
				}

				// see if we have data after the header and VLRs
				if (LASHeader.UserDataAfterHeaderSize > 0) {
					// write extra bytes in header...technically not allowed in 1.4
					fwrite(LASHeader.UserDataAfterHeader, sizeof(unsigned char), LASHeader.UserDataAfterHeaderSize, f);
				}
			}
			else {
				// offset to start of data
				fseek(f, LASHeader.OffsetToData - 2, SEEK_SET);

				// write start of data
				fwrite(startofdata, sizeof(unsigned char), 2, f);
			}

			Rewind();

			while (ReadNextRecord(&pt, &LASFields)) {
				if (LastPointIsWithheld())
					continue;

				if (SingleLine && LASFields.PointSourceID != LineNumber)
					continue;

				if (TimeSlice && (LASFields.GPSTime < StartTime || LASFields.GPSTime >= StopTime))
					continue;

				if (m_FileFormat == LASDATA && CleanLASPoints) {
					// check for points outside header extent
					if (pt.X < m_LASFile.Header.MinX - 0.25 * m_LASFile.Header.XScaleFactor || pt.X > m_LASFile.Header.MaxX + 0.25 * m_LASFile.Header.XScaleFactor ||
						pt.Y < m_LASFile.Header.MinY - 0.25 * m_LASFile.Header.YScaleFactor || pt.Y > m_LASFile.Header.MaxY + 0.25 * m_LASFile.Header.YScaleFactor ||
						pt.Elevation < m_LASFile.Header.MinZ - 0.25 * m_LASFile.Header.ZScaleFactor || pt.Elevation > m_LASFile.Header.MaxZ + 0.25 * m_LASFile.Header.ZScaleFactor)
						continue;

					// check for withheld points
					if (LastPointIsWithheld())
						continue;

					// check GPS time
					if ((m_LASFile.Header.Reserved.V11.Reserved & 1) == 0 && (m_LASFile.Header.PointDataFormatID == 1 || m_LASFile.Header.PointDataFormatID == 3 || m_LASFile.Header.PointDataFormatID >= 4)) {
						if (m_LASFile.PointRecord.GPSTime < 0.0 || m_LASFile.PointRecord.GPSTime > 604800.0) {
							continue;
						}
					}

					// check return number...must be 1-15
					if (pt.ReturnNumber <= 0 || pt.ReturnNumber > 15)
						continue;
				}

				if (m_FileFormat == LASDATA) {
					if (pt.ReturnNumber > 0 && pt.ReturnNumber < 15)
						LASReturnCounts[pt.ReturnNumber - 1] ++;
					// modified code for V1.4...12/21/2016
					else if (pt.ReturnNumber == 0)
						LASReturnCounts[0] ++;
					// end of modified code for V1.4...12/21/2016
					else
						LASReturnCounts[14] ++;

					// compute RawXYZ
					m_LASFile.PointRecord.RawX = I32_QUANTIZE((pt.X - LASHeader.XOffset) / LASHeader.XScaleFactor);
					m_LASFile.PointRecord.RawY = I32_QUANTIZE((pt.Y - LASHeader.YOffset) / LASHeader.YScaleFactor);
					m_LASFile.PointRecord.RawZ = I32_QUANTIZE(((double) pt.Elevation - LASHeader.ZOffset) / LASHeader.ZScaleFactor);

					// update raw min/max...needed to provide correct min/max for header
					lxMin = min(m_LASFile.PointRecord.RawX, lxMin);
					lyMin = min(m_LASFile.PointRecord.RawY, lyMin);
					lzMin = min(m_LASFile.PointRecord.RawZ, lzMin);
					lxMax = max(m_LASFile.PointRecord.RawX, lxMax);
					lyMax = max(m_LASFile.PointRecord.RawY, lyMax);
					lzMax = max(m_LASFile.PointRecord.RawZ, lzMax);

					m_LASFile.PointRecord.Write(f, LASHeader.PointDataFormatID);

					LASPointRecordCount ++;
				}
				else {
					if (pt.ReturnNumber > 0 && pt.ReturnNumber <= 5) {
						LASPt.RawX = (long) ((pt.X - LASHeader.XOffset) / LASHeader.XScaleFactor);
						LASPt.RawY = (long) ((pt.Y - LASHeader.YOffset) / LASHeader.YScaleFactor);
						LASPt.RawZ = (long) (((double) pt.Elevation - LASHeader.ZOffset) / LASHeader.ZScaleFactor);

						// update raw min/max...needed to provide correct min/max for header
						lxMin = min(LASPt.RawX, lxMin);
						lyMin = min(LASPt.RawY, lyMin);
						lzMin = min(LASPt.RawZ, lzMin);
						lxMax = max(LASPt.RawX, lxMax);
						lyMax = max(LASPt.RawY, lyMax);
						lzMax = max(LASPt.RawZ, lzMax);

						LASPt.Intensity = (unsigned short) pt.Intensity;
						LASPt.ReturnNumber = pt.ReturnNumber;
						LASPt.ScanAngleRank = (char) pt.NadirAngle;

						// if reading LAS files, maintain the returns info specific to LAS
						if (m_FileFormat == LASDATA && PreserveLASInfo) {
							LASPt.NumberOfReturns = m_LASFile.PointRecord.NumberOfReturns;
							LASPt.ScanDirectionFlag = m_LASFile.PointRecord.ScanDirectionFlag;
							LASPt.EdgeOfFlightLineFlag = m_LASFile.PointRecord.EdgeOfFlightLineFlag;
							LASPt.Classification = m_LASFile.PointRecord.Classification;
							LASPt.FileMarker = m_LASFile.PointRecord.FileMarker;
						}

						// write point
						LASPt.Write(f);
					}
				}
			}
			Rewind();

			if (m_FileFormat == LASDATA) {
				// modified code for V1.4...12/21/2016
				// delete memory for extra stuff in/after header
				if (LASHeader.UserDataInHeaderSize > 0) {
					delete [] LASHeader.UserDataInHeader;
					LASHeader.UserDataInHeaderSize = 0;
					LASHeader.UserDataInHeader = NULL;
				}

				if (LASHeader.UserDataAfterHeaderSize > 0) {
					delete [] LASHeader.UserDataAfterHeader;
					LASHeader.UserDataAfterHeaderSize = 0;
					LASHeader.UserDataAfterHeader = NULL;
				}
				// end of modified code for V1.4...12/21/2016

				
				fclose(f);

				// compute stuff
				double ULASH_MinX = ((double) lxMin * LASHeader.XScaleFactor) + LASHeader.XOffset;
				double ULASH_MinY = ((double) lyMin * LASHeader.YScaleFactor) + LASHeader.YOffset;
				double ULASH_MinZ = ((double) lzMin * LASHeader.ZScaleFactor) + LASHeader.ZOffset;
				double ULASH_MaxX = ((double) lxMax * LASHeader.XScaleFactor) + LASHeader.XOffset;
				double ULASH_MaxY = ((double) lyMax * LASHeader.YScaleFactor) + LASHeader.YOffset;
				double ULASH_MaxZ = ((double) lzMax * LASHeader.ZScaleFactor) + LASHeader.ZOffset;
				double ULASH_OffsetX = LASHeader.XOffset;
				double ULASH_OffsetY = LASHeader.YOffset;
				double ULASH_OffsetZ = LASHeader.ZOffset;
				
				// update header values
				UpdateLASHeader(OutputFileName, TRUE, TRUE, TRUE, TRUE, 
					ULASH_MinX,
					ULASH_MinY,
					ULASH_MinZ,
					ULASH_MaxX,
					ULASH_MaxY,
					ULASH_MaxZ,
					ULASH_OffsetX,
					ULASH_OffsetY,
					ULASH_OffsetZ,
					LASReturnCounts,
					LASPointRecordCount);

				// copy EVLRs from first input file to output file
				if (LASHeader.VersionMajor == 1 && LASHeader.VersionMinor >= 4) {
					CopyEVLRs(m_FileName, OutputFileName);
				}
			}
			else {
				// compute new min/max values and re-write header
				LASHeader.MinX = ((double) lxMin * LASHeader.XScaleFactor) + LASHeader.XOffset;
				LASHeader.MinY = ((double) lyMin * LASHeader.YScaleFactor) + LASHeader.YOffset;
				LASHeader.MinZ = ((double) lzMin * LASHeader.ZScaleFactor) + LASHeader.ZOffset;
				LASHeader.MaxX = ((double) lxMax * LASHeader.XScaleFactor) + LASHeader.XOffset;
				LASHeader.MaxY = ((double) lyMax * LASHeader.YScaleFactor) + LASHeader.YOffset;
				LASHeader.MaxZ = ((double) lzMax * LASHeader.ZScaleFactor) + LASHeader.ZOffset;

				fseek(f, 0, SEEK_SET);

				LASHeader.Write(f);

				fclose(f);
			}

			if (CreateIndex) {
				CDataIndex index;
				index.CreateIndex(OutputFileName, 10, 256, 256, (int) LASHeader.NumberOfPointRecords, LASHeader.MinX, LASHeader.MaxX, LASHeader.MinY, LASHeader.MaxY, LASHeader.MinZ, LASHeader.MaxZ, TRUE);
			}

			return(TRUE);
		}

		return(FALSE);
	}
	return(FALSE);
}
#endif

int CLidarData::GetFileFormat()
{
	if (m_Valid)
		return(m_FileFormat);

	return(-1);
}

BOOL CLidarData::UpdateLASHeader(LPCTSTR FileName, BOOL UpdateExtent, BOOL UpdateOffsets, BOOL UpdateReturnCounts, BOOL UpdateTotalReturns, double minx, double miny, double minz, double maxx, double maxy, double maxz, double offsetx, double offsety, double offsetz, unsigned __int64* Rn, unsigned __int64 TotalReturns)
{
	BOOL retcode = FALSE;

	// 5/31/2024 This version of the function shoujld be used for LAS V1.4+
	// 
	// can't use LASzip DLL to do this...

	// open file...assume it is LAS or LAZ format, header is the same for both and compression doesn't matter for the header
	// this function really doesn't fit in the CLidarData class because it directly modifies a LAS file without using any of the other class methods to open the file
	// use it with care and make sure that the file given in FileName isn't already open in another instance of the CLidarData class
	//
	// open the file and read the header
	// CLidarData.Open() can't be used since it opens the file for read only
	if (m_LASFile.VerifyFormat(FileName)) {
		// open file
		m_LASFile.m_FileHandle = fopen(FileName, "rb+");
		if (m_LASFile.m_FileHandle) {
			retcode = m_LASFile.Header.Read(m_LASFile.m_FileHandle);
		}
	}

	if (retcode) {
		// modify header
		if (UpdateExtent) {
			m_LASFile.Header.MinX = minx;
			m_LASFile.Header.MinY = miny;
			m_LASFile.Header.MinZ = minz;
			m_LASFile.Header.MaxX = maxx;
			m_LASFile.Header.MaxY = maxy;
			m_LASFile.Header.MaxZ = maxz;
		}

		if (UpdateOffsets) {
			m_LASFile.Header.XOffset = offsetx;
			m_LASFile.Header.YOffset = offsety;
			m_LASFile.Header.ZOffset = offsetz;
		}

		if (UpdateReturnCounts) {
			for (int i = 0; i < 15; i ++) 
				m_LASFile.Header.NumberOfPointsByReturn[i] = Rn[i];
		}

		if (UpdateTotalReturns) {
			m_LASFile.Header.NumberOfPointRecords = TotalReturns;
		}

		// jump to the beginning of the file and write header
		fseek(m_LASFile.m_FileHandle, 0, SEEK_SET);
		m_LASFile.Header.Write(m_LASFile.m_FileHandle);

		// close file
		fclose(m_LASFile.m_FileHandle);

		return(TRUE);
	}

	return(FALSE);
}

BOOL CLidarData::UpdateLASHeader(LPCTSTR FileName, BOOL UpdateExtent, BOOL UpdateOffsets, BOOL UpdateReturnCounts, BOOL UpdateTotalReturns, double minx, double miny, double minz, double maxx, double maxy, double maxz, double offsetx, double offsety, double offsetz, unsigned long R1, unsigned long R2, unsigned long R3, unsigned long R4, unsigned long R5, unsigned long TotalReturns)
{
	BOOL retcode = FALSE;

	// 5/31/2024 This version of the function shoujld be used for LAS V1.0-1.3
	// 
	// can't use LASzip DLL to do this...

	// open file...assume it is LAS or LAZ format, header is the same for both and compression doesn't matter for the header
	// this function really doesn't fit in the CLidarData class because it directly modifies a LAS file without using any of the other class methods to open the file
	// use it with care and make sure that the file given in FileName isn't already open in another instance of the CLidarData class
	//
	// open the file and read the header
	// CLidarData.Open() can't be used since it opens the file for read only
	if (m_LASFile.VerifyFormat(FileName)) {
		// open file
		m_LASFile.m_FileHandle = fopen(FileName, "rb+");
		if (m_LASFile.m_FileHandle) {
			retcode = m_LASFile.Header.Read(m_LASFile.m_FileHandle);
		}
	}

	if (retcode) {
		// modify header
		if (UpdateExtent) {
			m_LASFile.Header.MinX = minx;
			m_LASFile.Header.MinY = miny;
			m_LASFile.Header.MinZ = minz;
			m_LASFile.Header.MaxX = maxx;
			m_LASFile.Header.MaxY = maxy;
			m_LASFile.Header.MaxZ = maxz;
		}

		if (UpdateOffsets) {
			m_LASFile.Header.XOffset = offsetx;
			m_LASFile.Header.YOffset = offsety;
			m_LASFile.Header.ZOffset = offsetz;
		}

		if (UpdateReturnCounts) {
			m_LASFile.Header.NumberOfPointsByReturn[0] = R1;
			m_LASFile.Header.NumberOfPointsByReturn[1] = R2;
			m_LASFile.Header.NumberOfPointsByReturn[2] = R3;
			m_LASFile.Header.NumberOfPointsByReturn[3] = R4;
			m_LASFile.Header.NumberOfPointsByReturn[4] = R5;

/*			// deal with extended point counts for version 1.4+ files
			if (m_LASFile.Header.VersionMajor == 1 && m_LASFile.Header.VersionMinor >= 4) {
				// set legacy counts to 0
				m_LASFile.Header.NumberOfPointsByReturn[0] = 0;
				m_LASFile.Header.NumberOfPointsByReturn[1] = 0;
				m_LASFile.Header.NumberOfPointsByReturn[2] = 0;
				m_LASFile.Header.NumberOfPointsByReturn[3] = 0;
				m_LASFile.Header.NumberOfPointsByReturn[4] = 0;

				// set extended counts
				m_LASFile.Header.ExtendedNumberOfPointsByReturn[0] = R1;
				m_LASFile.Header.ExtendedNumberOfPointsByReturn[1] = R2;
				m_LASFile.Header.ExtendedNumberOfPointsByReturn[2] = R3;
				m_LASFile.Header.ExtendedNumberOfPointsByReturn[3] = R4;
				m_LASFile.Header.ExtendedNumberOfPointsByReturn[4] = R5;

			}
*/		}

		if (UpdateTotalReturns) {
			m_LASFile.Header.NumberOfPointRecords = TotalReturns;

/*			// deal with extended point counts for version 1.4+ files
			if (m_LASFile.Header.VersionMajor == 1 && m_LASFile.Header.VersionMinor >= 4) {
				// set legacy counts to 0
				m_LASFile.Header.NumberOfPointRecords = 0;

				// set extended counts
				m_LASFile.Header.ExtendedNumberOfPointRecords = TotalReturns;
			}
*/		}

		// jump to the beginning of the file and write header
		fseek(m_LASFile.m_FileHandle, 0, SEEK_SET);
		m_LASFile.Header.Write(m_LASFile.m_FileHandle);

		// close file
		fclose(m_LASFile.m_FileHandle);

		return(TRUE);
	}

	return(FALSE);
}

BOOL CLidarData::PopulateLASzipHeader(laszip_header *LASzip_header, CLASPublicHeaderBlock *OldLASHeader)
{
    laszip_U32 i;

	LASzip_header->file_source_ID = OldLASHeader->Reserved.V11.FileSourceID;
	LASzip_header->global_encoding = OldLASHeader->Reserved.V11.Reserved;
	LASzip_header->project_ID_GUID_data_1 = OldLASHeader->GUIDData1;
	LASzip_header->project_ID_GUID_data_2 = OldLASHeader->GUIDData2;
	LASzip_header->project_ID_GUID_data_3 = OldLASHeader->GUIDData3;
	memcpy(LASzip_header->project_ID_GUID_data_4, OldLASHeader->GUIDData4, 8);
	LASzip_header->version_major = OldLASHeader->VersionMajor;
	LASzip_header->version_minor = OldLASHeader->VersionMinor;
	memcpy(LASzip_header->system_identifier, OldLASHeader->SystemIdentifier, 32);
	memcpy(LASzip_header->generating_software, OldLASHeader->GeneratingSoftware, 32);
	LASzip_header->file_creation_day = OldLASHeader->FlightDateJulian;
	LASzip_header->file_creation_year = OldLASHeader->Year;
	LASzip_header->header_size = OldLASHeader->HeaderSize;
	LASzip_header->offset_to_point_data = OldLASHeader->OffsetToData;
//	LASzip_header->number_of_variable_length_records = OldLASHeader->NumberOfVariableLengthRecords;
	LASzip_header->point_data_format = OldLASHeader->PointDataFormatID;
	LASzip_header->point_data_record_length = OldLASHeader->PointDataRecordLength;
	LASzip_header->number_of_point_records = (unsigned int) OldLASHeader->NumberOfPointRecords;
	for (i = 0; i < 5; i++) {
		LASzip_header->number_of_points_by_return[i] = (unsigned int) OldLASHeader->NumberOfPointsByReturn[i];
	}
	LASzip_header->x_scale_factor = OldLASHeader->XScaleFactor;
	LASzip_header->y_scale_factor = OldLASHeader->YScaleFactor;
	LASzip_header->z_scale_factor = OldLASHeader->ZScaleFactor;
	LASzip_header->x_offset = OldLASHeader->XOffset;
	LASzip_header->y_offset = OldLASHeader->YOffset;
	LASzip_header->z_offset = OldLASHeader->ZOffset;
	LASzip_header->max_x = OldLASHeader->MaxX;
	LASzip_header->min_x = OldLASHeader->MinX;
	LASzip_header->max_y = OldLASHeader->MaxY;
	LASzip_header->min_y = OldLASHeader->MinY;
	LASzip_header->max_z = OldLASHeader->MaxZ;
	LASzip_header->min_z = OldLASHeader->MinZ;

	// deal with version specific stuff
	if (OldLASHeader->VersionMajor == 1 && OldLASHeader->VersionMinor >= 3) {
		// LAS 1.3 and higher only
		LASzip_header->start_of_waveform_data_packet_record = OldLASHeader->WaveformStart;
	}

	if (OldLASHeader->VersionMajor == 1 && OldLASHeader->VersionMinor >= 4) {
		// LAS 1.4 and higher only
		LASzip_header->start_of_first_extended_variable_length_record = OldLASHeader->StartOfExtendedVLR;
		LASzip_header->number_of_extended_variable_length_records = OldLASHeader->NumberOfExtendedVLRs;
		LASzip_header->extended_number_of_point_records = OldLASHeader->ExtendedNumberOfPointRecords;
		for (i = 0; i < 15; i++) {
			LASzip_header->extended_number_of_points_by_return[i] = OldLASHeader->ExtendedNumberOfPointsByReturn[i];
		}

		// set legacy counters to 0
		LASzip_header->number_of_point_records = 0;
		for (i = 0; i < 5; i++) {
			LASzip_header->number_of_points_by_return[i] = 0;
		}
	}

	return(TRUE);
}

void CLidarData::GetErrorMessage(CString &Err)
{
	switch (m_ReasonForInvalidFile) {
		case		NOATTEMPTTOOPEN:
			Err.Format("%s", "Program has not attempted to open file");
			break;
		case		BADFORMAT:
			Err.Format("%s", "Unrecognized file format (or format has errors)");
			break;
		case		NOTEXIST:
			Err.Format("%s", "File does not exist");
			break;
		case		LAZNODLL:
			Err.Format("%s", "Attempting to read LAZ files without LASzip.dll");
			break;
		case		CANTOPEN:
			Err.Format("%s", "Can't open file");
			break;
		case		NODATA:
			Err.Format("%s", "File contains ho data");
			break;
		case		NOFILEERROR:
			Err.Format("%s", "No error has occurred");
			break;
		default:
		case		UNKNOWN:
			Err.Format("%s", "Unknown error");
			break;
	}
}

BOOL CLidarData::LastPointIsWithheld()
{
	if (m_FileFormat == LASDATA) {
		if (m_HaveLASZIP_DLL) {
			if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4 && lasdll_header->point_data_format >= 6) {
				if ((lasdll_point->extended_classification_flags & 0x04) == 0x04) {
					return(TRUE);
				}
			}
			else {
				if (lasdll_point->withheld_flag) {
					return(TRUE);
				}
			}
		}
		else {
			if (m_LASFile.PointRecord.V11Withheld) {
				return(TRUE);
			}
		}
	}
	return(FALSE);
}

BOOL CLidarData::LastPointIsKeypoint()
{
	if (m_FileFormat == LASDATA) {
		if (m_HaveLASZIP_DLL) {
			if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4 && lasdll_header->point_data_format >= 6) {
				if ((lasdll_point->extended_classification_flags & 0x02) == 0x02) {
					return(TRUE);
				}
			}
			else {
				if (lasdll_point->keypoint_flag) {
					return(TRUE);
				}
			}
		}
		else {
			if (m_LASFile.PointRecord.V11KeyPoint) {
				return(TRUE);
			}
		}
	}
	return(FALSE);
}

BOOL CLidarData::LastPointIsSynthetic()
{
	if (m_FileFormat == LASDATA) {
		if (m_HaveLASZIP_DLL) {
			if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4 && lasdll_header->point_data_format >= 6) {
				if ((lasdll_point->extended_classification_flags & 0x01) == 0x01) {
					return(TRUE);
				}
			}
			else {
				if (lasdll_point->synthetic_flag) {
					return(TRUE);
				}
			}
		}
		else {
			if (m_LASFile.PointRecord.V11Synthetic) {
				return(TRUE);
			}
		}
	}
	return(FALSE);
}

BOOL CLidarData::LastPointIsOverlap()
{
	// no overlap bit in point formats 0-5
	if (m_FileFormat == LASDATA) {
		if (m_HaveLASZIP_DLL) {
			if (lasdll_header->version_major == 1 && lasdll_header->version_minor >= 4 && lasdll_header->point_data_format >= 6) {
				if ((lasdll_point->extended_classification_flags & 0x08) == 0x08) {
					return(TRUE);
				}
			}
		}
		else {
			if (m_LASFile.Header.VersionMajor == 1 && m_LASFile.Header.VersionMinor >= 4 && m_LASFile.Header.PointDataFormatID >= 6) {
				if (m_LASFile.PointRecord.V14Overlap) {
					return(TRUE);
				}
			}
		}
	}
	return(FALSE);
}


BOOL CLidarData::IsCOPC()
{
	return(m_IsCOPCFormat);
}


BOOL CLidarData::RegisterCOPCExtent(double minx, double miny, double maxx, double maxy)
{
	if (IsValid() && IsCOPC()) {
		m_COPCExtent.SetAll(minx, miny, maxx, maxy);
		return(TRUE);
	}
	return(FALSE);
}


uint64_t CLidarData::QueryForCOPCChunks(double spacing, BOOL verbose)
{
	uint64_t cnt = 0;
	if (IsValid() && IsCOPC() && m_COPCExtent.IsValid()) {
		m_COPCCurrentChunk = -1;
		m_COPCPointIndex = 0;
		cnt = m_COPCIndex.QueryChunks(m_COPCExtent.m_MinX, m_COPCExtent.m_MinY, m_COPCExtent.m_MaxX, m_COPCExtent.m_MaxY, spacing);
		if (verbose && cnt > 0)
			m_COPCIndex.PrintChunkTable(true);
	}
	return(cnt);
}


BOOL CLidarData::ReadNextCOPCRecord(LIDARRETURN* ldat, LAS_RETURNFIELDS* LASdat)
{
	if (!IsValid() || !IsCOPC() || !m_COPCExtent.IsValid())
		return(FALSE);

	int64_t nextChunk;

	if (m_COPCCurrentChunk < 0) {
		// make sure we have active chunks
		if (!m_COPCIndex.NumberOfActiveChunks())
			return(FALSE);

		// jump to first chunk
		for (uint64_t chunk = 0; chunk < m_COPCIndex.ChunkCount(); chunk++) {
			if (m_COPCIndex.ChunkStatus(chunk) == FULLOVERLAP || m_COPCIndex.ChunkStatus(chunk) == PARTIALOVERLAP) {
				m_COPCCurrentChunk = chunk;

				// jump to first point in chunk
				laszip_seek_point(lasdll_reader, m_COPCIndex.FirstPointIndex(m_COPCCurrentChunk));

				m_COPCPointIndex = 0;

				break;
			}
		}
	}

	// have we read the last point in the current chunk
	if (m_COPCPointIndex == m_COPCIndex.ChunkPointCount(m_COPCCurrentChunk)) {
		// jump to next chunk
		nextChunk = -1;
		for (uint64_t chunk = m_COPCCurrentChunk + 1; chunk < m_COPCIndex.ChunkCount(); chunk++) {
			if (m_COPCIndex.ChunkStatus(chunk) == FULLOVERLAP || m_COPCIndex.ChunkStatus(chunk) == PARTIALOVERLAP) {
				nextChunk = chunk;

				// jump to first point in chunk
				laszip_seek_point(lasdll_reader, m_COPCIndex.FirstPointIndex(nextChunk));

				m_COPCPointIndex = 0;

				break;
			}
		}
		if (nextChunk >= 0)
			m_COPCCurrentChunk = nextChunk;
		else
			return(FALSE);
	}

	// read points until we have one in the extent
	while (ReadNextRecord(ldat, LASdat)) {
		m_COPCPointIndex++;
		if (m_COPCIndex.ChunkStatus(m_COPCCurrentChunk) == FULLOVERLAP)
			return(TRUE);
		else {
			if (m_COPCExtent.IsPointWithin(ldat->X, ldat->Y))
				return(TRUE);
		}

		// have we read the last point in the current chunk
		if (m_COPCPointIndex == m_COPCIndex.ChunkPointCount(m_COPCCurrentChunk)) {
			// jump to next chunk
			nextChunk = -1;
			for (uint64_t chunk = m_COPCCurrentChunk + 1; chunk < m_COPCIndex.ChunkCount(); chunk++) {
				if (m_COPCIndex.ChunkStatus(chunk) == FULLOVERLAP || m_COPCIndex.ChunkStatus(chunk) == PARTIALOVERLAP) {
					nextChunk = chunk;

					// jump to first point in chunk
					laszip_seek_point(lasdll_reader, m_COPCIndex.FirstPointIndex(nextChunk));

					m_COPCPointIndex = 0;

					break;
				}
			}
			if (nextChunk >= 0)
				m_COPCCurrentChunk = nextChunk;
			else
				return(FALSE);
		}
	}
	return(FALSE);
}


BOOL CLidarData::IsCOPCExtentValid()
{
	return(m_COPCExtent.IsValid());
}
