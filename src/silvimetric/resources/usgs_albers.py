"""The canonical, pixel-is-area USGS CONUS Albers grid profile.

The profile uses the upper-left outer pixel edge of the USGS/NLCD Albers
grid.  Keeping this anchor fixed is what makes a pixel address mean the same
place in every independently-built Silvimetric database.  The storage root is
the complete CONUS grid; normal shatter planning remains bounded to the input
footprint and never enumerates the complete root.
"""

from __future__ import annotations

import pyproj

from .bounds import Bounds


USGS_ALBERS_CRS = "EPSG:5070+5703"
"""NAD83 / Conus Albers + NAVD88 height."""

USGS_ALBERS_HORIZONTAL_CRS = pyproj.CRS.from_epsg(5070)
"""The horizontal component used when transforming two-dimensional bounds."""

# These are *outer pixel edges*, not cell centres.  They are the established
# USGS/NLCD CONUS grid anchor and complete extent at 30 m resolution.
USGS_ALBERS_TOP_LEFT_X = -2_493_045.0
USGS_ALBERS_TOP_LEFT_Y = 3_310_005.0
USGS_ALBERS_CONUS_BOUNDS = Bounds(
    USGS_ALBERS_TOP_LEFT_X,
    177_285.0,
    2_342_655.0,
    USGS_ALBERS_TOP_LEFT_Y,
)

