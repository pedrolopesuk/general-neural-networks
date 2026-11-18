import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import build_materials
import build_niagara
import build_input
import build_ui
import build_level
import build_meshes

if __name__ == "__main__":
    build_materials.build_neuron_material()
    build_materials.build_hero_neuron_materials()
    build_meshes.build_neuron_meshes()
    build_materials.build_holon_shell_material()
    build_materials.build_edge_and_pulse_materials()
    build_materials.build_backdrop_material()
    build_niagara.build_niagara_systems()
    build_input.build_enhanced_input()
    build_ui.build_ui_assets()
    build_level.build_level()
