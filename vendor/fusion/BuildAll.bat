rem script to build 32- and 64-bit release versions of GridMetrics and CloudMetrics
rem This will only run from a VS command prompt

:GridMetrics
msbuild FUSION_Metrics\FUSION_Metrics.sln -target:GridMetrics -property:Configuration=Release -property:Platform=x86
msbuild FUSION_Metrics\FUSION_Metrics.sln -target:GridMetrics -property:Configuration=Release -property:Platform=x64

:CloudMetrics
msbuild FUSION_Metrics\FUSION_Metrics.sln -target:CloudMetrics -property:Configuration=Release -property:Platform=x86
msbuild FUSION_Metrics\FUSION_Metrics.sln -target:CloudMetrics -property:Configuration=Release -property:Platform=x64
