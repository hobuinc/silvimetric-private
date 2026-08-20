import numpy as np

from ..metric import Metric


def m_mean(data, *args):
    if len(data) == 0:
        return np.nan
    return np.mean(data)


def m_variance(data, *args):
    # copy FUSION's variance approach
    denom = data.count() - 1
    if denom == 0:
        return np.nan
    num = ((data - data.mean()) ** 2).sum()
    return num / denom


def m_skewness(data, *args):
    # copy FUSION's approximation of skewness
    count = data.count()
    if count < 2:
        return np.nan
    stddev = np.std(data, ddof=1)
    denom = (count - 1) * stddev**3
    if denom == 0:
        return np.nan
    num = ((data - data.mean()) ** 3).sum()
    return num / denom


def m_kurtosis(data, *args):
    # copy FUSION's approximation of kurtosis
    count = data.count()
    if count < 2:
        return np.nan
    stddev = np.std(data, ddof=1)
    denom = (count - 1) * stddev**4
    if denom == 0:
        return np.nan
    num = ((data - data.mean()) ** 4).sum()
    return num / denom


mean = Metric(name='mean', dtype=np.float32, method=m_mean)
variance = Metric(name='variance', dtype=np.float32, method=m_variance)
skewness = Metric(name='skewness', dtype=np.float32, method=m_skewness)
kurtosis = Metric(name='kurtosis', dtype=np.float32, method=m_kurtosis)

product_moments: dict[str, Metric] = dict(
    mean=mean, variance=variance, skewness=skewness, kurtosis=kurtosis
)
