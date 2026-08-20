import numpy as np
from scipy import stats

from ..metric import Metric
from .p_moments import mean


def m_mode(data, *args):
    """Return FUSION GridMetrics' 64-bin mode.

    FUSION scales the full data range to bin indexes 0 through 63, rather
    than using 64 equal-width NumPy histogram bins.  Its representative value
    is the *lower scaled bin position*, and ties select the lowest index.
    """
    values = np.asarray(data, dtype=float)
    if values.size == 0:
        return np.nan

    minimum = values.min()
    maximum = values.max()
    if minimum == maximum:
        return minimum

    bin_count = 64
    scaled = ((values - minimum) / (maximum - minimum)) * (bin_count - 1)
    bins = np.bincount(
        np.clip(scaled.astype(int), 0, bin_count - 1), minlength=bin_count
    )
    mode_bin = int(np.argmax(bins))
    return minimum + (mode_bin / (bin_count - 1)) * (maximum - minimum)


def m_median(data, *args):
    return np.median(data)


def m_min(data, *args):
    return np.min(data)


def m_max(data, *args):
    return np.max(data)


def m_stddev(data, *args):
    if data.count() < 2:
        return np.nan
    # FUSION uses sqrt(sum((x - mean)^2) / (n - 1)).
    return np.std(data, ddof=1)


def m_cv(data, *args):
    stddev, mean = args
    return np.divide(stddev, mean)


def m_crr(data, *args):
    mean, minimum, maximum = args
    den = maximum - minimum
    if den == 0:
        return 0.0
    return (mean - minimum) / den


def m_sqmean(data):
    return np.sqrt(np.mean(np.square(data)))


def m_cumean(data):
    # GridMetrics computes cube-root(abs(sum(x^3)) / n), not the mean of
    # absolute cubes.  They only differ when the selected values are mixed
    # positive and negative.
    return np.cbrt(np.abs(np.sum(np.power(data, 3))) / data.count())


def m_mad_median(data, *args):
    return stats.median_abs_deviation(data, nan_policy='propagate')


# TODO what to do if mode has 2 values?
def m_mad_mode(data, *args):
    return stats.median_abs_deviation(
        data, center=stats.mode, nan_policy='propagate'
    )


mode = Metric('mode', np.float32, m_mode)
median = Metric('median', np.float32, m_median)
minimum = Metric('min', np.float32, m_min)
maximum = Metric('max', np.float32, m_max)
stddev = Metric('stddev', np.float32, m_stddev)
cv = Metric('cv', np.float32, m_cv, [stddev, mean])
crr = Metric('canopy_relief_ratio', np.float32, m_crr, [mean, minimum, maximum])
sqmean = Metric('sqmean', np.float32, m_sqmean)
cumean = Metric('cumean', np.float32, m_cumean)

statistics: dict[str, Metric] = dict(
    mode=mode,
    median=median,
    min=minimum,
    max=maximum,
    stddev=stddev,
    cv=cv,
    crr=crr,
    sqmean=sqmean,
    cumean=cumean,
)
