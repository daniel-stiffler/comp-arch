import re
from glob import glob

def main():
    files = glob("*_small_1c/stats.log")

    scheme12_regex = "L2\.(scheme[12]_[1234]x)_s(\d+)_w(\d+) = (\d+)"
    uncompressed_regex = "L2\.(uncompressed_1x)_s(\d+)_w(\d+) = (\d+)"
    evict_bc_write_regex = "L2\.(evict_bc_write)_s(\d+) = (\d+)"

    for f in files:
        print("Processing data in file @{}".format(f))

        scheme_totals = {
            "scheme1_1x": 0, "scheme1_2x": 0, "scheme1_3x": 0, "scheme1_4x": 0,
            "scheme2_1x": 0, "scheme2_2x": 0, "scheme2_3x": 0, "scheme2_4x": 0,
            "uncompressed_1x": 0}

        set_stats_totals = {
            "evict_bc_write": 0}

        with open(f, "r") as fin:
            for line in fin:
                scheme12_match = re.match(scheme12_regex, line)
                uncompressed_match = re.match(uncompressed_regex, line)
                evict_bc_write_match = re.match(evict_bc_write_regex, line)

                if (scheme12_match):
                    scheme, set_index, way, value = scheme12_match.groups()
                    scheme_totals[scheme] += int(value)
                elif (uncompressed_match):
                    scheme, set_index, way, value = uncompressed_match.groups()
                    scheme_totals[scheme] += int(value)
                elif (evict_bc_write_match):
                    stat, set_index, value = evict_bc_write_match.groups()
                    set_stats_totals[stat] += int(value)

        print("Test @{} had the following data\n\t{}\n\t{}".format(f,
                                                                   scheme_totals,
                                                                   set_stats_totals))

if __name__ == "__main__":
    main()
