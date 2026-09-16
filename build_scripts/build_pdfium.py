#!/usr/bin/env python3
"""
PDFium Build Orchestrator
Handles GN configuration, Ninja build, and artifact extraction for cross-platform ARM/X64.
"""

import os
import sys
import subprocess
import platform
import shutil
import argparse
from pathlib import Path
from typing import Optional, List, Dict, Tuple

class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    END = '\033[0m'

def log_info(msg: str):
    print(f"{Colors.BLUE}[INFO]{Colors.END} {msg}")

def log_success(msg: str):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.END} {msg}")

def log_warning(msg: str):
    print(f"{Colors.YELLOW}[WARNING]{Colors.END} {msg}")

def log_error(msg: str):
    print(f"{Colors.RED}[ERROR]{Colors.END} {msg}")

def log_step(msg: str):
    print(f"{Colors.CYAN}[STEP]{Colors.END} {msg}")

class BuildConfig:
    """Configuration for a specific build target"""
    def __init__(self, 
                 target_os: str,
                 target_cpu: str,
                 is_debug: bool = False,
                 is_component_build: bool = False,
                 enable_xfa: bool = True,
                 enable_v8: bool = False,
                 use_sysroot: bool = False,
                 use_skia: bool = False):
        self.target_os = target_os
        self.target_cpu = target_cpu
        self.is_debug = is_debug
        self.is_component_build = is_component_build
        self.enable_xfa = enable_xfa
        self.enable_v8 = enable_v8
        self.use_sysroot = use_sysroot
        self.use_skia = use_skia
        
    @property
    def build_dir_name(self) -> str:
        config = "debug" if self.is_debug else "release"
        return f"out/{config}_{self.target_os}_{self.target_cpu}"
    
    @property
    def gn_args(self) -> List[str]:
        # XFA requires V8. If V8 is disabled, XFA must also be disabled.
        effective_xfa = self.enable_xfa and self.enable_v8
        
        args = [
            f'target_os = "{self.target_os}"',
            f'target_cpu = "{self.target_cpu}"',
            f'is_debug = {str(self.is_debug).lower()}',
            f'is_component_build = {str(self.is_component_build).lower()}',
            f'pdf_enable_xfa = {str(effective_xfa).lower()}',
            f'pdf_enable_v8 = {str(self.enable_v8).lower()}',
            f'pdf_use_skia = {str(self.use_skia).lower()}',
            'pdf_use_skia_paths = false',
            'use_sysroot = false',
            'use_custom_libcxx = false',
            'use_safe_libcxx = true',
            'v8_enable_sandbox = false',
            'clang_use_chrome_plugins = false',
            'pdf_is_complete_lib = true',
        ]
        
        if self.is_debug:
            # Keep full type/local debug info and never strip symbols.
            args.append('symbol_level = 2')
        else:
            args.append('symbol_level = 1')
        
        # Platform-specific args
        if self.target_os == "win":
            args.append('target_environment = "win32"')
            
        return args

class PDFiumBuilder:
    def __init__(self, repo_root: Path, config: BuildConfig):
        self.repo_root = repo_root
        self.config = config
        self.pdfium_wrapper_dir = repo_root / "third_party" / "pdfium"
        # Resolve actual PDFium source root (handle gclient nested dir)
        self.pdfium_dir = self.pdfium_wrapper_dir / "pdfium"
        if not self.pdfium_dir.exists():
            self.pdfium_dir = self.pdfium_wrapper_dir
            
        self.depot_tools_dir = repo_root / "depot_tools"
        self.build_dir = self.pdfium_dir / config.build_dir_name
        self.system = platform.system().lower()
        self.machine = platform.machine().lower()
        
    def setup_environment(self) -> dict:
        """Setup build environment"""
        env = os.environ.copy()
        
        # Add depot_tools to PATH
        depot_tools_path = str(self.depot_tools_dir)
        if depot_tools_path not in env.get("PATH", ""):
            env["PATH"] = f"{depot_tools_path}{os.pathsep}{env.get('PATH', '')}"
            
        env["DEPOT_TOOLS_UPDATE"] = "0"
        
        # Platform-specific environment
        if self.system == "windows":
            # Ensure we're in a Visual Studio environment
            env["GYP_MSVS_VERSION"] = "2022"
            
        return env

    def run_cmd(self, cmd: List[str], cwd: Optional[Path] = None, env: Optional[dict] = None) -> Tuple[int, str, str]:
        """Run command with real-time output streaming"""
        log_info(f"Running: {' '.join(cmd)}")
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=cwd or self.pdfium_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env or self.setup_environment(),
                bufsize=1,
            )
            stdout_lines = []
            stderr_lines = []
            
            def read_stream(stream, lines_list, prefix=""):
                for line in iter(stream.readline, ''):
                    if line:
                        line = line.rstrip('\n')
                        lines_list.append(line)
                        if prefix:
                            print(f"{prefix}{line}")
                        else:
                            print(line)
                stream.close()
            
            import threading
            stdout_thread = threading.Thread(target=read_stream, args=(proc.stdout, stdout_lines, ""))
            stderr_thread = threading.Thread(target=read_stream, args=(proc.stderr, stderr_lines, "[STDERR] "))
            stdout_thread.start()
            stderr_thread.start()
            
            proc.wait(timeout=3600)
            stdout_thread.join()
            stderr_thread.join()
            
            return proc.returncode, "\n".join(stdout_lines), "\n".join(stderr_lines)
        except subprocess.TimeoutExpired:
            proc.kill()
            return -1, "", "Command timed out after 1 hour"
        except Exception as e:
            return -1, "", str(e)

    def generate_gn_args(self) -> bool:
        """Generate GN build arguments"""
        log_step(f"Generating GN args for {self.config.target_os}/{self.config.target_cpu}...")
        
        self.build_dir.mkdir(parents=True, exist_ok=True)
        args_gn = self.build_dir / "args.gn"
        args_gn.write_text("\n".join(self.config.gn_args) + "\n")
        
        log_info(f"GN args written to {args_gn}")
        for arg in self.config.gn_args:
            log_info(f"  {arg}")
        return True

    def run_gn_gen(self) -> bool:
        """Run gn gen to generate Ninja build files"""
        log_step("Running gn gen...")
        
        gn_cmd = [sys.executable, str(self.depot_tools_dir / "gn.py"), "gen", str(self.build_dir)]
        rc, _, stderr = self.run_cmd(gn_cmd)
        
        if rc != 0:
            log_error(f"gn gen failed: {stderr}")
            return False
            
        log_success("GN build files generated")
        return True

    def run_ninja_build(self, target: str = "pdfium") -> bool:
        """Run ninja to build PDFium"""
        log_step(f"Building target '{target}' with ninja...")
        
        # Determine number of parallel jobs
        cpu_count = os.cpu_count() or 4
        jobs = min(cpu_count, 16)  # Limit to avoid memory issues
        
        ninja_cmd = [
            sys.executable, str(self.depot_tools_dir / "ninja.py"),
            "-C", str(self.build_dir),
            "-j", str(jobs),
            target
        ]
        
        rc, _, stderr = self.run_cmd(ninja_cmd)
        
        if rc != 0:
            log_error(f"Ninja build failed: {stderr}")
            return False
            
        log_success(f"Ninja build completed for target: {target}")
        return True

    def extract_artifacts(self) -> bool:
        """Extract built artifacts to standard locations"""
        log_step("Extracting artifacts...")
        
        # Define source and destination paths
        artifacts_dir = self.repo_root / "third_party" / "pdfium"
        include_dir = artifacts_dir / "include"
        lib_dir = artifacts_dir / "lib"
        bin_dir = artifacts_dir / "bin"
        
        include_dir.mkdir(parents=True, exist_ok=True)
        lib_dir.mkdir(parents=True, exist_ok=True)
        bin_dir.mkdir(parents=True, exist_ok=True)
        
        # Copy headers
        src_include = self.pdfium_dir / "public"
        if src_include.exists():
            for header in src_include.rglob("*.h"):
                rel_path = header.relative_to(src_include)
                dst = include_dir / rel_path
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(header, dst)
            log_info(f"Copied headers to {include_dir}")
        
        # Platform-specific artifact copying
        if self.system == "windows":
            return self._extract_windows_artifacts(lib_dir, bin_dir)
        elif self.system == "darwin":
            return self._extract_macos_artifacts(lib_dir, bin_dir)
        else:
            return self._extract_linux_artifacts(lib_dir, bin_dir)

    def _artifact_name(self, fname: str) -> str:
        """Return the destination name for a built artifact.

        Debug builds get a '_debug' suffix on library files so they never
        overwrite the release variants in third_party/pdfium/lib|bin. Release
        builds keep their plain names; non-library executables are unchanged."""
        if not self.config.is_debug:
            return fname
        stem, ext = os.path.splitext(fname)
        if ext.lower() in (".a", ".lib", ".so", ".dylib", ".dll"):
            return f"{stem}_debug{ext}"
        return fname

    def _extract_windows_artifacts(self, lib_dir: Path, bin_dir: Path) -> bool:
        """Extract Windows artifacts (.lib, .dll)"""
        # Find the built files
        patterns = {
            "pdfium.lib": lib_dir,
            "pdfium.dll": bin_dir,
        }
        
        for pattern, dst_dir in patterns.items():
            found = list(self.build_dir.rglob(pattern))
            if found:
                for f in found:
                    name = self._artifact_name(f.name)
                    shutil.copy2(f, dst_dir / name)
                    log_info(f"Copied {f.name} -> {name} to {dst_dir}")
            else:
                log_warning(f"Artifact not found: {pattern}")
                
        return True

    def _extract_macos_artifacts(self, lib_dir: Path, bin_dir: Path) -> bool:
        """Extract macOS artifacts (.dylib, .a)"""
        patterns = {
            "libpdfium.dylib": bin_dir,
            "libpdfium.a": lib_dir,
            "pdfium": bin_dir,  # executable if built
        }
        
        for pattern, dst_dir in patterns.items():
            found = list(self.build_dir.rglob(pattern))
            if found:
                for f in found:
                    # Prefer lib_dir for static libs and dylibs to keep pdfium_wrapper happy
                    target_dir = lib_dir if (f.suffix == ".a" or f.suffix == ".dylib") else dst_dir
                    name = self._artifact_name(f.name)
                    shutil.copy2(f, target_dir / name)
                    log_info(f"Copied {f.name} -> {name} to {target_dir}")
            else:
                log_warning(f"Artifact not found: {pattern}")
                    
        return True

    def _extract_linux_artifacts(self, lib_dir: Path, bin_dir: Path) -> bool:
        """Extract Linux artifacts (.so, .a)"""
        patterns = {
            "libpdfium.so": bin_dir,
            "libpdfium.a": lib_dir,
            "pdfium": bin_dir,
        }
        
        for pattern, dst_dir in patterns.items():
            found = list(self.build_dir.rglob(pattern))
            if found:
                for f in found:
                    name = self._artifact_name(f.name)
                    shutil.copy2(f, dst_dir / name)
                    log_info(f"Copied {f.name} -> {name} to {dst_dir}")
                    
        return True

    def build(self) -> bool:
        """Run complete build process"""
        log_info("=" * 60)
        log_info(f"Building PDFium for {self.config.target_os}/{self.config.target_cpu}")
        log_info(f"Config: {'Debug' if self.config.is_debug else 'Release'}")
        log_info("=" * 60)
        
        steps = [
            ("Apply PDFium extensions", self.apply_extensions),
            ("Generate GN args", self.generate_gn_args),
            ("Run GN gen", self.run_gn_gen),
            ("Run Ninja build", lambda: self.run_ninja_build("pdfium")),
            ("Extract artifacts", self.extract_artifacts),
        ]
        
        for name, step_func in steps:
            if not step_func():
                log_error(f"Step failed: {name}")
                return False
                
        log_success("PDFium build completed successfully!")
        return True

    def apply_extensions(self) -> bool:
        """Apply custom PDFium extensions"""
        log_step("Applying PDFium extensions...")
        
        apply_script = self.repo_root / "build_scripts" / "apply_patches.py"
        extensions_dir = self.repo_root / "pdfium_extensions"
        
        if not apply_script.exists():
            log_warning("Patch script not found, skipping extensions")
            return True
            
        if not extensions_dir.exists():
            log_warning("Extensions directory not found, skipping")
            return True
            
        cmd = [sys.executable, str(apply_script), str(self.pdfium_dir), str(extensions_dir)]
        if not self.config.enable_v8:
            cmd.append("--no-v8")
            
        rc, _, stderr = self.run_cmd(cmd)
        
        if rc != 0:
            log_error(f"Failed to apply extensions: {stderr}")
            return False
            
        log_success("PDFium extensions applied")
        return True

def get_supported_configs() -> Dict[str, List[BuildConfig]]:
    """Get supported build configurations for current platform"""
    system = platform.system().lower()
    machine = platform.machine().lower()
    
    # Normalize machine name
    if machine in ("x86_64", "amd64"):
        machine = "x64"
    elif machine in ("aarch64", "arm64"):
        machine = "arm64"
    elif machine in ("i386", "i686"):
        machine = "x86"
        
    configs = {}
    
    if system == "windows":
        configs["native"] = [
            BuildConfig("win", machine, is_debug=False),
            BuildConfig("win", machine, is_debug=True),
        ]
        # Cross-compile configs
        if machine == "x64":
            configs["cross"] = [
                BuildConfig("win", "arm64", is_debug=False),
            ]
            
    elif system == "darwin":
        configs["native"] = [
            BuildConfig("mac", machine, is_debug=False),
            BuildConfig("mac", machine, is_debug=True),
        ]
        # Cross-compile: x64 can build arm64 and vice versa on Apple Silicon
        if machine == "arm64":
            configs["cross"] = [
                BuildConfig("mac", "x64", is_debug=False),
            ]
        elif machine == "x64":
            configs["cross"] = [
                BuildConfig("mac", "arm64", is_debug=False),
            ]
            
    else:  # Linux
        configs["native"] = [
            BuildConfig("linux", machine, is_debug=False),
            BuildConfig("linux", machine, is_debug=True),
        ]
        # Cross-compile requires toolchain setup
        if machine == "x64":
            configs["cross"] = [
                BuildConfig("linux", "arm64", is_debug=False),
                BuildConfig("linux", "arm", is_debug=False),
            ]
            
    return configs

def main():
    parser = argparse.ArgumentParser(description="PDFium Build Orchestrator")
    parser.add_argument("--target-os", choices=["win", "mac", "linux"], help="Target OS")
    parser.add_argument("--target-cpu", choices=["x64", "arm64", "arm", "x86"], help="Target CPU")
    parser.add_argument("--debug", action="store_true", help="Build debug configuration")
    parser.add_argument("--release", action="store_true", help="Build release configuration (default)")
    parser.add_argument("--component", action="store_true", help="Build as component (shared library)")
    parser.add_argument("--no-xfa", action="store_true", help="Disable XFA support")
    parser.add_argument("--enable-v8", action="store_true", help="Enable V8 JavaScript engine")
    parser.add_argument("--use-skia", action="store_true", help="Enable internal PDFium Skia rendering")
    parser.add_argument("--list-configs", action="store_true", help="List supported configurations")
    parser.add_argument("--all", action="store_true", help="Build all native configurations")
    
    args = parser.parse_args()
    
    repo_root = Path(__file__).parent.parent
    
    # List supported configs
    if args.list_configs:
        configs = get_supported_configs()
        print("Supported build configurations:")
        for category, config_list in configs.items():
            print(f"  {category}:")
            for config in config_list:
                print(f"    {config.target_os}/{config.target_cpu} ({'debug' if config.is_debug else 'release'})")
        return 0
    
    # Determine build configs
    configs_to_build = []
    
    if args.all:
        configs = get_supported_configs()
        configs_to_build = configs.get("native", [])
    elif args.target_os and args.target_cpu:
        # --debug and --release are mutually exclusive selectors; the default
        # (neither flag) is a release build.
        is_debug = args.debug and not args.release
        configs_to_build = [BuildConfig(
            target_os=args.target_os,
            target_cpu=args.target_cpu,
            is_debug=is_debug,
            is_component_build=args.component,
            enable_xfa=not args.no_xfa,
            enable_v8=args.enable_v8,
            use_skia=args.use_skia,
        )]
    else:
        # Default: native release build
        configs = get_supported_configs()
        native_configs = configs.get("native", [])
        release_configs = [c for c in native_configs if not c.is_debug]
        configs_to_build = release_configs if release_configs else native_configs[:1]
    
    if not configs_to_build:
        log_error("No valid build configurations found for this platform")
        return 1
    
    # Run builds
    success_count = 0
    for config in configs_to_build:
        builder = PDFiumBuilder(repo_root, config)
        if builder.build():
            success_count += 1
        else:
            log_error(f"Build failed for {config.target_os}/{config.target_cpu}")
            
    if success_count == len(configs_to_build):
        log_success(f"All {success_count} build(s) completed successfully!")
        return 0
    else:
        log_error(f"Only {success_count}/{len(configs_to_build)} builds succeeded")
        return 1

if __name__ == "__main__":
    sys.exit(main())