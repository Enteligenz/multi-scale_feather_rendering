import re
import subprocess

def modify_scene(
    input_path: str,
    output_path: str,
    spp: int | None = None,
    threshold_lod: float | None = None,
    image_id: str | None = None,
    seed: int | None = None,
    lod: bool | None = None,
):
    with open(input_path, "r") as f:
        content = f.read()

    if spp is not None:
        content = re.sub(
            r'(<sampler[^>]*\bcount=")[^"]*(")',
            rf'\g<1>{spp}\2',
            content,
        )

    if seed is not None:
        if re.search(r'<sampler\b[^>]*\bseed="', content):
            # seed attribute already present — replace it
            content = re.sub(
                r'(<sampler\b[^>]*\bseed=")[^"]*(")',
                rf'\g<1>{seed}\2',
                content,
            )
        else:
            # seed attribute absent — insert it after count="..."
            content = re.sub(
                r'(<sampler\b[^>]*\bcount="[^"]*")',
                rf'\g<1> seed="{seed}"',
                content,
            )

    if threshold_lod is not None:
        content = re.sub(
            r'(\bthresholdLOD=")[^"]*(")',
            rf'\g<1>{threshold_lod}\2',
            content,
        )

    if lod is not None:
        content = re.sub(
            r'(\blod=")[^"]*(")',
            rf'\g<1>{"true" if lod else "false"}\2',
            content,
        )

    if image_id is not None:
        def _replace_image_id(m):
            return re.sub(
                r'(<image\s+id=")[^"]*(")',
                rf'\g<1>{image_id}\2',
                m.group(0),
            )
        content = re.sub(
            r'<integrator\b.*?</integrator>',
            _replace_image_id,
            content,
            flags=re.DOTALL,
        )

    with open(output_path, "w") as f:
        f.write(content)

    print(f"Written to {output_path}")


LOD_SWITCHES = {
    1: "Low detail using combined switch",
    2: "Low detail using PDF only switch",
    3: "Low detail using TraveledDistance only switch",
    4: "Low detail using bounce depth as switch",
    5: "Always low detail",
    6: "Footprint method",
    7: "Derived from Philipp Ziegler's Bsc Thesis",
    8: "Low detail using combined switch with weighted-down distance",
}

LOD_SWITCHES_PROSE = {
    1: "Combined",
    2: "PDF",
    3: "Distance",
    4: "Bounce",
    5: "OnlyLD",
    6: "Footprint",
    7: "NewCombined",
    8: "CombinedLessDist",
}

def set_lod_switch(src_file: str, switch: int) -> None:
    if switch not in LOD_SWITCHES:
        raise ValueError(f"Invalid LOD switch: {switch}. Must be 1-8.")

    with open(src_file, "r") as f:
        content = f.read()

    def replace_switch(m):
        indent = m.group(1)
        code = m.group(2)
        trail = m.group(3)
        switch_num = int(m.group(4))
        if switch_num == switch:
            return f"{indent}{code}{{{trail}"
        else:
            return f"{indent}// {code}{{{trail}"

    new_content = re.sub(
        r'^(\s*)(?:// )?(if \(m_useLOD\b[^{]*)\{( // [^\n]*\[(\d+)\])$',
        replace_switch,
        content,
        flags=re.MULTILINE,
    )

    if new_content == content:
        print(f"Warning: no LOD switch lines found in {src_file}")

    with open(src_file, "w") as f:
        f.write(new_content)

    print(f"LOD switch set to [{switch}]: {LOD_SWITCHES[switch]}")

def run_scene(
    input_xml: str,
    output_xml: str,
    executable: str,
    executable_src_file: str,
    runs: list[dict],
    current_lod_switch: int | None = None,
) -> int | None:
    current_lod_switch = None

    for i, params in enumerate(runs):
        print(f"\n--- Run {i + 1}/{len(runs)}: {params} ---")

        try:
            lod_switch = params.get("lod_switch")
            scene_params = {k: v for k, v in params.items() if k != "lod_switch"}

            if lod_switch is not None and lod_switch != current_lod_switch:
                set_lod_switch(executable_src_file, lod_switch)
                current_lod_switch = lod_switch

                print("Rebuilding with ninja...")
            # {"spp": 256, "threshold_lod": 0.1,  "image_id": "run_high", "lod_switch": 1},
                build_result = subprocess.run(
                    ["ninja"],
                    cwd="./build",
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                print(build_result.stdout, end="")
                if build_result.returncode != 0:
                    print(f"ninja failed with code {build_result.returncode}")
                    print(build_result.stderr, end="")
                    break

            modify_scene(input_xml, output_xml, **scene_params)

            result = subprocess.run(
                [executable, output_xml],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            print(result.stdout, end="")

            # log_name = f"{params['spp']}_{params['threshold_lod']}_{params['image_id']}_lod{lod_switch}.txt"
            if params['lod']: # Low detail
                log_name = f"{params['spp']}_LD_{params['threshold_lod']}_lod{LOD_SWITCHES_PROSE[lod_switch]}_seed{params['seed']}_{params['image_id']}.txt"
            else: # High detail
                log_name = f"{params['spp']}_HD_seed{params['seed']}_{params['image_id']}.txt"

            render_match  = re.search(r'render done! took ([\d.]+) seconds', result.stdout)
            parsing_match = re.search(r'parsing done! took ([\d.]+) seconds', result.stdout)

            if result.returncode != 0:
                print(f"Test.exe exited with code {result.returncode}")
                log_content = "error"
            elif not render_match or not parsing_match:
                print("Could not find timing lines in output")
                log_content = "error"
            else:
                log_content = (
                    f"render:  {render_match.group(1)} seconds\n"
                    f"parsing: {parsing_match.group(1)} seconds\n"
                )

            with open(log_name, "w") as f:
                f.write(log_content)
            print(f"Results written to {log_name}")
        except Exception as e:
            import traceback
            print(f"Run {i + 1} failed: {e!r}")
            traceback.print_exc()
            continue

    return current_lod_switch

if __name__ == "__main__":
    # INPUT_XML           = "./tests/multiple_feathers/hair_ball_single_closeup.xml"
    # OUTPUT_XML          = "./tests/multiple_feathers/hair_ball_single_closeup_modified.xml"
    INPUT_XML           = "./tests/multiple_feathers/hair_ball_single.xml"
    OUTPUT_XML          = "./tests/multiple_feathers/hair_ball_single_modified.xml"
    # INPUT_XML           = "./tests/owlbear/owlbear_baby.xml"
    # OUTPUT_XML          = "./tests/owlbear/owlbear_baby_modified.xml"
    EXECUTABLE          = "./build/unnamed" # Add .exe on Windows
    EXECUTABLE_SRC_FILE = "./src/shapes/accel.cpp"

    runs = [
        {"spp": 64,  "threshold_lod": 100, "image_id": "single_distance_100_64spp_seed_1", "lod_switch": 3, "seed": 1, "lod": True},
        {"spp": 1024,  "threshold_lod": 100, "image_id": "single_distance_100_1024spp_seed_1", "lod_switch": 3, "seed": 1, "lod": True},
        {"spp": 64,  "threshold_lod": 100, "image_id": "single_distance_100_64spp_seed_2", "lod_switch": 3, "seed": 2, "lod": True},
        {"spp": 1024,  "threshold_lod": 100, "image_id": "single_distance_100_1024spp_seed_2", "lod_switch": 3, "seed": 2, "lod": True},
        {"spp": 64,  "threshold_lod": 100, "image_id": "single_distance_100_64spp_seed_3", "lod_switch": 3, "seed": 3, "lod": True},
        {"spp": 1024,  "threshold_lod": 100, "image_id": "single_distance_100_1024spp_seed_3", "lod_switch": 3, "seed": 3, "lod": True},
        
    ]

    run_scene(
        input_xml=INPUT_XML,
        output_xml=OUTPUT_XML,
        executable=EXECUTABLE,
        executable_src_file=EXECUTABLE_SRC_FILE,
        runs=runs,
    )