#!/usr/bin/env python3
import argparse
import pathlib
import re
import shutil
import tempfile
import zipfile


SYNTH_ENTRY = """      <spirit:file>
        <spirit:name>hdl/verilog/floating_point_v7_1_rfs.v</spirit:name>
        <spirit:fileType>systemVerilogSource</spirit:fileType>
      </spirit:file>
"""

SIM_ENTRY = """      <spirit:file>
        <spirit:name>hdl/verilog/floating_point_v7_1_rfs.v</spirit:name>
        <spirit:fileType>systemVerilogSource</spirit:fileType>
        <spirit:userFileType>USED_IN_ipstatic</spirit:userFileType>
      </spirit:file>
"""

SYNTH_REF_FILESET = """    <spirit:fileSet>
      <spirit:name>xilinx_verilogsynthesis_xilinx_com_ip_floating_point_7_1__ref_view_fileset</spirit:name>
      <spirit:vendorExtensions>
        <xilinx:subCoreRef>
          <xilinx:componentRef xilinx:vendor="xilinx.com" xilinx:library="ip" xilinx:name="floating_point" xilinx:version="7.1">
            <xilinx:mode xilinx:name="copy_mode"/>
          </xilinx:componentRef>
        </xilinx:subCoreRef>
      </spirit:vendorExtensions>
    </spirit:fileSet>
"""

SIM_REF_FILESET = """    <spirit:fileSet>
      <spirit:name>xilinx_verilogbehavioralsimulation_xilinx_com_ip_floating_point_7_1__ref_view_fileset</spirit:name>
      <spirit:vendorExtensions>
        <xilinx:subCoreRef>
          <xilinx:componentRef xilinx:vendor="xilinx.com" xilinx:library="ip" xilinx:name="floating_point" xilinx:version="7.1">
            <xilinx:mode xilinx:name="copy_mode"/>
          </xilinx:componentRef>
        </xilinx:subCoreRef>
      </spirit:vendorExtensions>
    </spirit:fileSet>
"""


def patch_fileset(xml: str, fileset_name: str, entry: str) -> str:
    pattern = re.compile(
        rf"(<spirit:fileSet>\s*<spirit:name>{re.escape(fileset_name)}</spirit:name>)(.*?)(</spirit:fileSet>)",
        re.DOTALL,
    )
    match = pattern.search(xml)
    if not match:
        raise RuntimeError(f"fileset '{fileset_name}' not found")
    body = match.group(2)
    if "hdl/verilog/floating_point_v7_1_rfs.v" in body:
        return xml
    replacement = match.group(1) + body + entry + match.group(3)
    return xml[: match.start()] + replacement + xml[match.end() :]


def ensure_ref_fileset(xml: str, fileset_name: str, fileset_text: str, before_fileset_name: str) -> str:
    if f"<spirit:name>{fileset_name}</spirit:name>" in xml:
        return xml
    marker = f'    <spirit:fileSet>\n      <spirit:name>{before_fileset_name}</spirit:name>'
    index = xml.find(marker)
    if index < 0:
        raise RuntimeError(f"marker fileset '{before_fileset_name}' not found")
    return xml[:index] + fileset_text + xml[index:]


def ensure_view_ref(xml: str, view_name: str, ref_fileset_name: str, before_fileset_name: str) -> str:
    pattern = re.compile(
        rf"(<spirit:view>\s*<spirit:name>{re.escape(view_name)}</spirit:name>.*?)(<spirit:fileSetRef>\s*<spirit:localName>{re.escape(before_fileset_name)}</spirit:localName>\s*</spirit:fileSetRef>)(.*?</spirit:view>)",
        re.DOTALL,
    )
    match = pattern.search(xml)
    if not match:
        raise RuntimeError(f"view '{view_name}' not found")
    if ref_fileset_name in match.group(1) or ref_fileset_name in match.group(2) or ref_fileset_name in match.group(3):
        return xml
    ref_block = (
        "        <spirit:fileSetRef>\n"
        f"          <spirit:localName>{ref_fileset_name}</spirit:localName>\n"
        "        </spirit:fileSetRef>\n"
    )
    replacement = match.group(1) + ref_block + match.group(2) + match.group(3)
    return xml[: match.start()] + replacement + xml[match.end() :]


def main() -> int:
    parser = argparse.ArgumentParser(description="Patch xo component.xml to keep floating_point_v7_1_rfs.v in file sets.")
    parser.add_argument("xo", type=pathlib.Path)
    args = parser.parse_args()

    xo_path = args.xo.resolve()
    if not xo_path.is_file():
        raise FileNotFoundError(f"xo not found: {xo_path}")

    with tempfile.TemporaryDirectory(prefix="projectx_xo_patch_") as tmp:
        tmpdir = pathlib.Path(tmp)
        with zipfile.ZipFile(xo_path, "r") as zin:
            zin.extractall(tmpdir)

        component_paths = list(tmpdir.glob("ip_repo/*/component.xml"))
        if len(component_paths) == 0:
            return 0
        if len(component_paths) != 1:
            raise RuntimeError(f"expected exactly one component.xml, found {len(component_paths)}")

        component_path = component_paths[0]
        xml = component_path.read_text(encoding="utf-8")

        # If the xo does not even contain the shared floating-point Verilog,
        # there is nothing useful to patch here.
        fp_file = component_path.parent / "hdl/verilog/floating_point_v7_1_rfs.v"
        if not fp_file.exists():
            return 0

        xml = patch_fileset(xml, "xilinx_verilogsynthesis_view_fileset", SYNTH_ENTRY)
        xml = patch_fileset(xml, "xilinx_verilogbehavioralsimulation_view_fileset", SIM_ENTRY)
        xml = ensure_ref_fileset(
            xml,
            "xilinx_verilogsynthesis_xilinx_com_ip_floating_point_7_1__ref_view_fileset",
            SYNTH_REF_FILESET,
            "xilinx_verilogbehavioralsimulation_view_fileset",
        )
        xml = ensure_ref_fileset(
            xml,
            "xilinx_verilogbehavioralsimulation_xilinx_com_ip_floating_point_7_1__ref_view_fileset",
            SIM_REF_FILESET,
            "xilinx_softwaredriver_view_fileset",
        )
        xml = ensure_view_ref(
            xml,
            "xilinx_verilogsynthesis",
            "xilinx_verilogsynthesis_xilinx_com_ip_floating_point_7_1__ref_view_fileset",
            "xilinx_verilogsynthesis_view_fileset",
        )
        xml = ensure_view_ref(
            xml,
            "xilinx_verilogbehavioralsimulation",
            "xilinx_verilogbehavioralsimulation_xilinx_com_ip_floating_point_7_1__ref_view_fileset",
            "xilinx_verilogbehavioralsimulation_view_fileset",
        )
        component_path.write_text(xml, encoding="utf-8")

        backup = xo_path.with_suffix(xo_path.suffix + ".bak")
        shutil.copy2(xo_path, backup)

        with zipfile.ZipFile(xo_path, "w", compression=zipfile.ZIP_DEFLATED) as zout:
            for path in sorted(tmpdir.rglob("*")):
                if path.is_file():
                    zout.write(path, path.relative_to(tmpdir))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
