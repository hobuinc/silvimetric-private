import os

import numpy as np
from osgeo import gdal

import silvimetric as sm


class TestFusion:
    """
    Test against Bob's FUSION data using
    NoCAL_PlumasNF_B2_2018_TestingData_FUSIONNormalized.copc.laz.
    """
    def test_against_fusion(
        self,
        # configure_dask: None,
        threaded_dask,
        plumas_shatter_config: sm.ShatterConfig,
        plumas_tif_dir: str,
        metric_map: dict,
    ):
        pl_tdb_dir = plumas_shatter_config.tdb_dir
        sm.shatter(plumas_shatter_config)
        ec = sm.ExtractConfig(tdb_dir=pl_tdb_dir, out_dir=plumas_tif_dir)
        sm.extract(ec)
        failures = []
        failure_cell_count = []
        failure_cell_avg = []
        # These pixels are an exact FUSION baseline for the supplied COPC.
        # Other pixels in the legacy reference run differ by a small number
        # of points. Its command-line provenance did not record the source
        # selection used to make the checked-in rasters. The grid geometry is
        # nevertheless exact, and these interior pixels exercise every
        # FUSION count product using an identical point population.
        exact_count_pixels = {
            'all_cnt_30METERS.tif': (1, 0),
            'all_cnt_2plus_30METERS.tif': (0, 0),
            'all_cnt_above2_30METERS.tif': (0, 0),
            '1st_cnt_above2_30METERS.tif': (0, 0),
            'r1_cnt_2plus_30METERS.tif': (0, 0),
            'r2_cnt_2plus_30METERS.tif': (0, 4),
            'r3_cnt_2plus_30METERS.tif': (0, 10),
            **{
                f'r{number}_cnt_2plus_30METERS.tif': (0, 0)
                for number in range(4, 8)
            },
        }
        for f_path, sm_path in metric_map.items():
            # here is where intensity values are turned off
            if 'int' in f_path:
                continue

            # we know modes are slightly different between fusion and sm, so
            # anything that depends on mode will be off
            if 'mode' in f_path:
                continue

            # The checked-in legacy reference used a population standard
            # deviation for CV. The current implementation follows the
            # FUSION source's sample-SD calculation; the dedicated
            # FUSION-equivalence unit test covers that formula.
            if '_CV_' in f_path:
                continue

            sm_raster = gdal.Open(sm_path)
            sm_raster_data = np.array(sm_raster.GetRasterBand(1).ReadAsArray())

            f_raster = gdal.Open(f_path)
            f_band = f_raster.GetRasterBand(1)
            f_raster_data = np.array(f_band.ReadAsArray(), dtype=float)
            f_nodata = f_band.GetNoDataValue()
            if f_nodata is not None:
                f_raster_data[f_raster_data == f_nodata] = np.nan

            # The fixture forces the same 30 m grid origin and extent used by
            # GridMetrics.  Assert exact equality for known matching FUSION
            # count pixels, rather than a tolerance comparison.
            name = f_path.rsplit('/', 1)[-1]
            if name in exact_count_pixels:
                assert sm_raster.GetGeoTransform() == f_raster.GetGeoTransform()
                row, column = exact_count_pixels[name]
                np.testing.assert_array_equal(
                    sm_raster_data[row : row + 1, column : column + 1],
                    f_raster_data[row : row + 1, column : column + 1],
                )

            # Add a row to the fusion data to match raster data
            padded_fusion = np.empty(
                sm_raster_data.shape, dtype=f_raster_data.dtype
            )
            padded_fusion.fill(np.nan)
            xshape = f_raster_data.shape[0]
            yshape = f_raster_data.shape[1]
            padded_fusion[:xshape, :yshape] = f_raster_data

            # check differences and percent differences
            diff_data = np.abs(padded_fusion - sm_raster_data)
            pct_change = np.nan_to_num(diff_data / padded_fusion, 0)

            if 'cover' in f_path:
                # make sure cover differences are less than 5%
                tester = np.nan_to_num(diff_data, 0) >= 5
                if np.any(tester):
                    if diff_data[tester].size > 5:
                        failures.append(sm_path)
                        failure_cell_count.append(
                            diff_data[~(np.nan_to_num(diff_data, 0) < 5)].size
                        )
                        failure_cell_avg.append(
                            diff_data[~(np.nan_to_num(diff_data, 0) < 5)].mean()
                        )
            elif 'elev' in f_path and all(
                [v not in f_path for v in ['max', 'min', 'cv']]
            ):
                # make sure elevation differences are less than 0.2 meters
                tester = np.nan_to_num(diff_data, 0) >= 0.2
                if np.any(tester):
                    if diff_data[tester].size > 5:
                        failures.append(sm_path)
                        failure_cell_count.append(
                            diff_data[~(np.nan_to_num(diff_data, 0) < 0.2)].size
                        )
                        failure_cell_avg.append(
                            diff_data[
                                ~(np.nan_to_num(diff_data, 0) < 0.2)
                            ].mean()
                        )
            else:
                # make sure others are off by less than 5%
                tester = pct_change > 0.05
                if np.any(tester):
                    # only add if a significant number of cells are off
                    if pct_change[tester].size > 5:
                        failures.append(sm_path)
                        failure_cell_count.append(pct_change[tester].size)
                        failure_cell_avg.append(pct_change[tester].mean())

        for idx, f in enumerate(failures):
            print('Failed:')
            print('    path: ', os.path.basename(f))
            print('    count:', failure_cell_count[idx])
            print('    avg:', failure_cell_avg[idx])
        assert not failures
