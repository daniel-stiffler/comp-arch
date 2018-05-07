WITH stats AS (
SELECT
	nameid,
	substr(metricname, 0, instr(metricname, '_s')) AS scheme/*,
	substr(metricname, instr(metricname, '_s')+2, instr(metricname, '_w')-(instr(metricname, '_s')+2)) AS [set],
	substr(metricname, instr(metricname, '_w')+2) AS way*/
FROM [names]
WHERE 
	objectname='L2'
	AND (metricname REGEXP 'scheme[12]_[234]x_s\d+_w\d+' OR metricname REGEXP 'uncompressed_1x_s\d+_w\d+')
),
roi_start AS (
SELECT nameid, [value]
FROM [values]
WHERE prefixid=2 AND core=0
),
roi_end AS (
SELECT nameid, [value]
FROM [values]
WHERE prefixid=3 AND core=0
),
roi_start_stats AS (
SELECT scheme, SUM(value) AS scheme_total
FROM stats
INNER JOIN roi_start
USING (nameid)
GROUP BY scheme
),
roi_end_stats AS (
SELECT scheme, SUM(value) AS scheme_total
FROM stats
INNER JOIN roi_end
USING (nameid)
GROUP BY scheme
)

SELECT 
	roi_end_stats.scheme,
	roi_end_stats.scheme_total,
	roi_start_stats.scheme_total, 
	roi_end_stats.scheme_total - roi_start_stats.scheme_total AS delta
FROM roi_end_stats
LEFT JOIN roi_start_stats
ON (roi_end_stats.scheme=roi_start_stats.scheme OR roi_start_stats.scheme IS NULL)