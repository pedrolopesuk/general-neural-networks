import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import build_materials
import build_niagara
import build_level

if __name__ == "__main__":
    build_materials.build_neuron_material()
    build_materials.build_holon_shell_material()
    build_materials.build_edge_and_pulse_materials()
    build_materials.build_backdrop_material()
    build_niagara.build_niagara_systems()
    build_level.build_level()
