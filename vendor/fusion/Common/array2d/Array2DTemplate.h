// This is core/vbl/vbl_array_2d.h
#ifndef vbl_array_2d_h_
#define vbl_array_2d_h_
//:
// \file
// \brief Contains class for a templated 2d array
// \author Andrew W. Fitzgibbon, Oxford RRG
// \date   05 Aug 96
//
// \verbatim
// Modifications
// Peter Vanroose -13nov98- added copy constructor and assignment operator
// AWF 10/12/98 Added row/column store.  The old split was just too pedantic.
// PDA (Manchester) 21/03/2001: Tidied up the documentation
//
// Changed units used for writing array to PLANS format to indicate unknown units
//
// 5/23/2013 added RasterData to write_to_raster_file() to shift the origin from the ll corner to the center of the cell
// This is needed to allow calling programs to determine which type of data is being written to maintain alignment
//
// \endverbatim

//: simple 2D array
template <class T>
class vbl_array_2d
{
 public:

  //: Default constructor
  vbl_array_2d() { construct(); }

  //: Construct m-by-n array.
  vbl_array_2d(int rows, int cols) { construct(rows, cols); }

  //: Construct and fill an m-by-n array.
  vbl_array_2d(int rows, int cols, const T &v) { construct(rows, cols); fill(v);}

  //: Construct from a 2d array
  vbl_array_2d(vbl_array_2d<T> const &that) {
    construct(that.rows(), that.cols());
    operator=(that);
  }

  //: Destructor
  ~vbl_array_2d() { destruct(); }

  //: Assignment
  vbl_array_2d<T>& operator=(vbl_array_2d<T> const &that) {
	if (that.havememory()) {
      resize(that.rows(), that.cols());
	  if (havememory()) {
        for (int row = 0; row < num_rows_; ++ row)
          for (int col = 0; col < num_cols_; ++ col)
            rows_[row][col] = that.rows_[row][col];
	  }
	}
    return *this;
  }

  //: Comparison
  bool operator==(vbl_array_2d<T> const &that) const {
	if (!havememory() || !that.havememory())
	  return false;
    if (num_rows_ != that.num_rows_ || num_cols_ != that.num_cols_)
      return false;
    for (int row = 0; row < num_rows_; ++ row)
      for (int col = 0; col < num_cols_; ++ col)
        if (!( rows_[row][col] == that.rows_[row][col] )) // do not assume we have operator!=
          return false;
    return true;
  }

  //:
  bool operator!=(vbl_array_2d<T> const &that) const {
    return ! operator==(that);
  }

  //: test for valid memory
  bool havememory() {
	  return rows_ != NULL;
  }

  //: test for row/col in range of array
  bool validindex(int row, int col) {
	  if (rows_) {
		  if (row >= 0 && row < num_rows_ && col >= 0 && col < num_cols_)
			  return true;
	  }
	  return false;
  }

  //: print to ASCII file...added 6/1/06 RJM
  bool write_to_file(LPCTSTR FileName, int DecimalPlaces, BOOL BottomUp = FALSE) {
	double val;
	if (havememory()) {
		// open the file
		FILE* f = fopen(FileName, "wt");
		if (f) {
			for (int row = 0; row < num_rows_ ; ++ row) {
				for (int col = 0; col < num_cols_; ++ col) {
					if (BottomUp)
						val = (double) rows_[(num_rows_ - 1) - row][col];
					else
						val = (double) rows_[row][col];
					if (col == num_cols_ - 1)
						fprintf(f, "%.*lf\n", DecimalPlaces, val);
					else
						fprintf(f, "%.*lf,", DecimalPlaces, val);
				}
			}
			fclose(f);
			return true;
		}
	}
	return false;
  }

  //: print to PLANS DTM file...added 11/3/06 RJM
  //: modified to add buffer stuff 12/16/2008
  bool write_to_PlansDTM_file(LPCTSTR FileName, double llx, double lly, double cell_size, int startcol = 0, int startrow = 0, int endcol = 0, int endrow = 0) {
	float val;
	int row, col;
	if (havememory()) {
		// open the file
		FILE* f = fopen(FileName, "wb");
		if (f) {
			// build header and write to file
			long columns = num_cols_;
			long points = num_rows_;
			if (startcol + startrow + endcol + endrow != 0) {
				columns = (endcol - startcol) + 1;
				points = (endrow - startrow) + 1;
			}

			if (columns <= 0 || points <= 0) {
				fclose(f);
				DeleteFile(FileName);
				return false;
			}
			double column_spacing = cell_size;
			double point_spacing = cell_size;
			double origin_x = llx;
			double origin_y = lly;
			if (startcol + startrow + endcol + endrow != 0) {
				origin_x = llx + (cell_size * (double) startcol);
				origin_y = lly + (cell_size * (double) startrow);
			}
			double rotation = 0.0;
			double version = 1.0;
			short xy_units = 2;
			short z_units = 2;
			short z_bytes = 2;
			double max_z = -10000000.0;
			double min_z = 10000000.0;
			char name[61];
			char signature[21];
			strcpy(name, "2D Array");
			strcpy(signature, "PLANS-PC BINARY .DTM");

			// get min/max elevation
			if (startcol + startrow + endcol + endrow != 0) {
				for (col = startcol; col <= endcol; ++ col) {
					for (row = startrow; row <= endrow; ++ row) {
						if (rows_[row][col] > max_z)
							max_z = rows_[row][col];

						if (rows_[row][col] >= 0.0 && rows_[row][col] < min_z)
							min_z = rows_[row][col];
					}
				}
			}
			else {
				for (col = 0; col < num_cols_; ++ col) {
					for (row = 0; row < num_rows_; ++ row) {
						if (rows_[row][col] > max_z)
							max_z = rows_[row][col];

						if (rows_[row][col] >= 0.0 && rows_[row][col] < min_z)
							min_z = rows_[row][col];
					}
				}
			}

			fwrite(&signature, sizeof(char), 21, f);
			fwrite(&name, sizeof(char), 61, f);
			fwrite(&version, sizeof(float), 1, f);
			fwrite(&origin_x, sizeof(double), 1, f);
			fwrite(&origin_y, sizeof(double), 1, f);
			fwrite(&min_z, sizeof(double), 1, f);
			fwrite(&max_z, sizeof(double), 1, f);
			fwrite(&rotation, sizeof(double), 1, f);
			fwrite(&column_spacing, sizeof(double), 1, f);
			fwrite(&point_spacing, sizeof(double), 1, f);
			fwrite(&columns, sizeof(long), 1, f);
			fwrite(&points, sizeof(long), 1, f);
			fwrite(&xy_units, sizeof(short), 1, f);
			fwrite(&z_units, sizeof(short), 1, f);
			fwrite(&z_bytes, sizeof(short), 1, f);

			fseek(f, 200l, SEEK_SET);

			if (startcol + startrow + endcol + endrow != 0) {
				for (col = startcol; col <= endcol; ++ col) {
					for (row = startrow; row <= endrow; ++ row) {
						// work from bottom up
						val = (float) rows_[row][col];
//						val = (float) rows_[(num_rows_ - 1) - row][col];
						if (val < 0.0f)
							val = -1.0f;

						// write value
						fwrite(&val, sizeof(float), 1, f);
					}
				}
			}
			else {
				for (col = 0; col < num_cols_; ++ col) {
					for (row = 0; row < num_rows_; ++ row) {
						// work from bottom up
						val = (float) rows_[row][col];
//						val = (float) rows_[(num_rows_ - 1) - row][col];
						if (val < 0.0f)
							val = -1.0f;

						// write value
						fwrite(&val, sizeof(float), 1, f);
					}
				}
			}
			fclose(f);
			return true;
		}
	}
	return false;
  }

  //: print to ASCII raster file...added 6/1/06 RJM
  bool write_to_raster_file(LPCTSTR FileName, int DecimalPlaces, double llx, double lly, double cell_size, double nodata_value, int startcol = 0, int startrow = 0, int endcol = 0, int endrow = 0, BOOL RasterData = FALSE, double nodata_in_grid = -9999.0) {
	double val;
	int row, col;
	if (havememory()) {
		// open the file
		FILE* f = fopen(FileName, "wt");
		if (f) {
			int columns = num_cols_;
			int points = num_rows_;
			if (startcol + startrow + endcol + endrow != 0) {
				columns = (endcol - startcol) + 1;
				points = (endrow - startrow) + 1;
			}

			if (columns <= 0 || points <= 0) {
				fclose(f);
				DeleteFile(FileName);
				return false;
			}

			double origin_x = llx;
			double origin_y = lly;
			if (startcol + startrow + endcol + endrow != 0) {
				origin_x = llx + (cell_size * (double) startcol);
				origin_y = lly + (cell_size * (double) startrow);
			}

			// write header
			fprintf(f, "ncols %i\n", columns);
			fprintf(f, "nrows %i\n", points);
			if (RasterData) {
				fprintf(f, "xllcenter %.4lf\n", origin_x);
				fprintf(f, "yllcenter %.4lf\n", origin_y);
			}
			else {
				fprintf(f, "xllcorner %.4lf\n", origin_x);
				fprintf(f, "yllcorner %.4lf\n", origin_y);
			}
			fprintf(f, "cellsize %.4lf\n", cell_size);
			fprintf(f, "nodata_value %.*lf\n", DecimalPlaces, nodata_value);

			// write data
			if (startcol + startrow + endcol + endrow != 0) {
				for (row = endrow; row >= startrow; -- row) {
					for (col = startcol; col <= endcol; ++ col) {
						val = (double) rows_[row][col];

						// look for NODATA values
						if (nodata_in_grid != nodata_value) {
							if (val == nodata_in_grid)
								val = nodata_value;
						}

						if (col == num_cols_ - 1)
							fprintf(f, "%.*lf\n", DecimalPlaces, val);
						else
							fprintf(f, "%.*lf ", DecimalPlaces, val);
					}
				}
			}
			else {
				for (row = num_rows_ - 1; row >= 0 ; -- row) {
					for (col = 0; col < num_cols_; ++ col) {
						val = (double) rows_[row][col];

						// look for NODATA values
						if (nodata_in_grid != nodata_value) {
							if (val == nodata_in_grid)
								val = nodata_value;
						}

						if (col == num_cols_ - 1)
							fprintf(f, "%.*lf\n", DecimalPlaces, val);
						else
							fprintf(f, "%.*lf ", DecimalPlaces, val);
					}
				}
			}
			fclose(f);
			return true;
		}
	}
	return false;
  }

  //: smooth using n*n averaging window...added 6/1/06 RJM
  void smooth(int windowsize, vbl_array_2d<T> &temparray) {
	if (temparray.havememory()) {
		if (num_rows_ == temparray.num_rows_ && num_cols_ == temparray.num_cols_) {
			if (windowsize % 2 == 1) {
				T* values = new T[windowsize * windowsize];
				if (values) {
					double valuesum;
					int nindex, k, l;
					int row, col;
					int halfsize = windowsize / 2;
					for (row = 0; row < num_rows_; ++ row) {
						for (col = 0; col < num_cols_; ++ col) {
							nindex = 0;
							valuesum = 0.0;
							for (k = -halfsize; k <= halfsize; k ++) {
								for (l = -halfsize; l <= halfsize; l ++) {
									if (getwithrangecheck(row - k, col - l, values[nindex])) {
										valuesum += (double) values[nindex];
										nindex ++;
									}
								}
							}
							temparray[row][col] = (T) (valuesum / (double) nindex);
						}
					}

					delete [] values;

					for (row = 0; row < num_rows_; ++ row) {
						for (col = 0; col < num_cols_; ++ col) {
							rows_[row][col] = temparray[row][col];
						}
					}
				}
			}
		}
	}
  }

  //: fill with `value
  void fill(T value) {
	if (havememory()) {
      for (int row = 0; row < num_rows_; ++ row)
        for (int col = 0; col < num_cols_; ++ col)
          rows_[row][col] = value;
	}
  }

  //: change size.
  void resize(int rows, int cols) {
    if (rows != num_rows_ || cols != num_cols_) {
      destruct();
      construct(rows, cols);
    }
  }

  //: make as if default-constructed.
  void clear() {
    if (rows_) {
      destruct();
      construct();
    }
  }

  // Data Access---------------------------------------------------------------
  T const& operator() (int row, int col) const { return rows_[row][col]; }
  T      & operator() (int row, int col) { return rows_[row][col]; }

  void put(int row, int col, T const &x) { rows_[row][col] = x; }
  T    get(int row, int col) const { return rows_[row][col]; }
  
  // get value after checking for valid row/col
  bool getwithrangecheck(int row, int col, T &val) { 
	  if (validindex(row, col)) {
		  val = rows_[row][col];
		  return true;
	  }
	  else
		  return false;
  }

  T const* operator[] (int row) const { return rows_[row]; }
  T      * operator[] (int row) { return rows_[row]; }


  //: Return number of rows
  int rows() const { return num_rows_; }

  //: Return number of columns
  int cols() const { return num_cols_; }

  //: Return number of columns
  int columns() const { return num_cols_; }

  //: Return size = (number of rows) * (number of columns)
  int size() const { return num_rows_ * num_cols_; }

  T      *      * get_rows() { return rows_; }
  T const* const* get_rows() const { return rows_; }

  typedef T       *iterator;
  iterator begin() { return rows_[0]; }
  iterator end  () { return rows_[0] + num_cols_ * num_rows_; }

  typedef T const *const_iterator;
  const_iterator begin() const { return rows_[0]; }
  const_iterator end  () const { return rows_[0] + num_cols_ * num_rows_; }

 private:
  T** rows_;
  int num_rows_;
  int num_cols_;

  void construct() {
    rows_ = 0;
    num_rows_ = 0;
    num_cols_ = 0;
  }

	void construct(int rows, int cols) {
		num_rows_ = 0;
		num_cols_ = 0;
		if (rows && cols) {
			try {
				rows_ = new T * [rows];
			}
			catch (...) {
				rows_ = NULL;
			}

			if (rows_) {
				for (int row = 0; row < rows; row ++) {
					try {
						rows_[row] = new T[cols];
					}
					catch (...) {
						rows_[row] = NULL;
					}

					if (!rows_[row]) {
						for (int j = row - 1; j >= 0; j --)
							delete [] rows_[j];

						delete [] rows_;
						rows_ = NULL;
						break;
					}
				}
			}
			else
				rows_ = NULL;
		}
		else {
			rows_ = NULL;
		}
		if (rows_) {
			num_rows_ = rows;
			num_cols_ = cols;
		}
	}
/*	void construct(int rows, int cols) {
		num_rows_ = rows;
		num_cols_ = cols;
		if (rows && cols) {
			try {
				rows_ = new T * [rows];
			}
			catch (...) {
				rows_ = NULL;
			}

			if (rows_) {
				T* p;
				try {
					p = new T[rows * cols];
				}
				catch (...) {
					p = NULL;
				}

				if (p) {
					for (int row = 0; row < rows; ++row)
						rows_[row] = p + row * cols;
				}
				else {
					delete [] rows_;
					rows_ = NULL;
				}
			}
			else
				rows_ = NULL;
		}
		else {
			rows_ = NULL;
		}
	}
*/
	void destruct() {
		if (rows_) {
			for (int row = 0; row < num_rows_; row ++)
				delete [] rows_[row];
			
			delete [] rows_;
			rows_ = NULL;
		}
	}
/*	void destruct() {
		if (rows_) {
			delete [] rows_[0];
			delete [] rows_;
		}
	}
*/
};

#endif // vbl_array_2d_h_

