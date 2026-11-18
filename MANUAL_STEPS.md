# MetaBrain UE 5.8 — Asset Rebuild & Optional Manual Editor Steps

## 1. Zero-Click Automated Rebuild (Default)

Every `.uasset` and `.umap` in `/Content/MetaBrain/` is generated deterministically from Python editor scripts in `/Scripts/build_assets/`. **No manual editor steps are required** to build or run the project from a clean Git clone.

### Step 1 — Compile C++ Modules (`MetaBrainCore` + `MetaBrainViz`)
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MetaBrainUEEditor Win64 Development -Project="C:\Users\main\Documents\metabrain\MetaBrainUE.uproject" -WaitMutex -FromMsBuild
```

### Step 2 — Rebuild All Materials, Niagara Systems, Enhanced Input, Fonts, UI Assets & Level
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\main\Documents\metabrain\MetaBrainUE.uproject" -run=pythonscript -script="C:\Users\main\Documents\metabrain\Scripts\build_assets\build_all.py" -unattended -nop4 -nosplash
```

### Step 3 — Launch Interactive Visualizer
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\main\Documents\metabrain\MetaBrainUE.uproject" /Game/MetaBrain/Maps/L_MetaBrain -game -ResX=2560 -ResY=1440 -Windowed
```

---

## 2. Optional Editor Customization: Custom Niagara Scratchpad HLSL Wiring

Unreal Engine 5.8's Python API (`unreal.NiagaraSystem`) creates `NS_SynapseRibbons` and `NS_SignalPulses` cleanly, while internal Niagara Scratchpad pin graphs are editor-only. To guarantee deterministic rendering straight from scripts, `AMetaBrainSceneController` renders crisp instanced filaments (`EdgeFilamentHISM`) and pulses (`SignalPulseHISM`) **and** simultaneously streams live arrays every tick into `NS_SynapseRibbons` and `NS_SignalPulses` via `UNiagaraDataInterfaceArrayFunctionLibrary`:

- `NS_SynapseRibbons`: `User.EdgeStarts` (`Position Array`), `User.EdgeEnds` (`Position Array`), `User.EdgeWeights` (`Float Array`)
- `NS_SignalPulses`: `User.PulsePositions` (`Position Array`)

If you wish to wire a custom Niagara Ribbon emitter directly inside the Niagara Editor instead of `EdgeFilamentHISM`:
1. Open `/Game/MetaBrain/FX/NS_SynapseRibbons` in Unreal Editor.
2. In the **User Parameters** panel, confirm or add:
   - `User.EdgeStarts` (`Array of Position`)
   - `User.EdgeEnds` (`Array of Position`)
   - `User.EdgeWeights` (`Array of Float`)
3. Right-click in the System Overview -> **Add Emitter** -> **Minimal** (`EM_Ribbons`).
4. Under **Render**, remove `Sprite Renderer`, click **+** -> **Ribbon Renderer**, and set **Material** to `/Game/MetaBrain/Materials/M_EdgeFilament`.
5. Under **Particle Update**, click **+** -> **New Scratch Pad Module** (`ReadEdgePair`):
   - Add Inputs: `EdgeStarts` (`Position Array`), `EdgeEnds` (`Position Array`), `EdgeWeights` (`Float Array`).
   - Map `Particles.RibbonID` to `ExecIndex / 2`, `Particles.RibbonLinkOrder` to `ExecIndex % 2` (`0.0` at `EdgeStarts[Idx]`, `1.0` at `EdgeEnds[Idx]`), and `Particles.RibbonWidth` to `0.65`.
6. Click **Apply** and **Save**, then set `EdgeFilamentHISM->SetVisibility(false)` in `AMetaBrainSceneController::BeginPlay` if desired.
