#!/usr/bin/env python3
"""
Bootstrap script for PDFium build environment.
Handles depot_tools installation, toolchain verification, and environment setup.
"""

import os
import sys
import subprocess
import platform
import shutil
import urllib.request
import zipfile
import tarfile
import threading
from pathlib import Path
from typing import Optional, List, Tuple

class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    END = '\033[0m'

def log_info(msg: str):
    print(f"{Colors.BLUE}[INFO]{Colors.END} {msg}")

def log_success(msg: str):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.END} {msg}")

def log_warning(msg: str):
    print(f"{Colors.YELLOW}[WARNING]{Colors.END} {msg}")

def log_error(msg: str):
    print(f"{Colors.RED}[ERROR]{Colors.END} {msg}")

class BootstrapManager:
    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self.depot_tools_dir = repo_root / "depot_tools"
        self.pdfium_dir = repo_root / "third_party" / "pdfium"
        self.system = platform.system().lower()
        self.machine = platform.machine().lower()
        
    def run_cmd(self, cmd: List[str], cwd: Optional[Path] = None, env: Optional[dict] = None) -> Tuple[int, str, str]:
        """Run command with real-time output streaming"""
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=cwd or self.repo_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env=env or os.environ,
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
            
            stdout_thread = threading.Thread(target=read_stream, args=(proc.stdout, stdout_lines, ""))
            stderr_thread = threading.Thread(target=read_stream, args=(proc.stderr, stderr_lines, "[STDERR] "))
            stdout_thread.start()
            stderr_thread.start()
            
            proc.wait()
            stdout_thread.join()
            stderr_thread.join()
            
            return proc.returncode, "\n".join(stdout_lines), "\n".join(stderr_lines)
        except Exception as e:
            return -1, "", str(e)

    def check_command(self, cmd: str) -> bool:
        """Check if command exists in PATH"""
        return shutil.which(cmd) is not None

    def install_depot_tools(self) -> bool:
        """Install depot_tools if not present"""
        if self.depot_tools_dir.exists():
            log_info(f"depot_tools already exists at {self.depot_tools_dir}")
            return True
            
        log_info("Installing depot_tools...")
        
        if self.system == "windows":
            # On Windows, depot_tools is a batch file collection
            depot_tools_url = "https://storage.googleapis.com/chrome-infra/depot_tools.zip"
            zip_path = self.repo_root / "depot_tools.zip"
            
            try:
                log_info(f"Downloading from {depot_tools_url}")
                urllib.request.urlretrieve(depot_tools_url, zip_path)
                
                with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                    zip_ref.extractall(self.repo_root)
                    
                zip_path.unlink()
                log_success("depot_tools installed successfully")
                return True
            except Exception as e:
                log_error(f"Failed to install depot_tools: {e}")
                return False
        else:
            # On Linux/macOS, clone from git
            try:
                self.run_cmd([
                    "git", "clone", 
                    "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
                    str(self.depot_tools_dir)
                ])
                log_success("depot_tools cloned successfully")
                return True
            except Exception as e:
                log_error(f"Failed to clone depot_tools: {e}")
                return False

    def setup_environment(self) -> dict:
        """Setup environment variables for depot_tools"""
        env = os.environ.copy()
        
        # Add depot_tools to PATH
        depot_tools_path = str(self.depot_tools_dir)
        if depot_tools_path not in env.get("PATH", ""):
            env["PATH"] = f"{depot_tools_path}{os.pathsep}{env.get('PATH', '')}"
            
        # Disable auto-update for consistent builds
        env["DEPOT_TOOLS_UPDATE"] = "0"
        
        # Python configuration
        env["PYTHONPATH"] = f"{depot_tools_path}{os.pathsep}{env.get('PYTHONPATH', '')}"
        
        return env

    def verify_toolchain(self, env: dict) -> bool:
        """Verify required toolchain components"""
        log_info("Verifying toolchain...")
        
        # Check Python
        if not self.check_command("python3") and not self.check_command("python"):
            log_error("Python 3 not found in PATH")
            return False
            
        # Check Git
        if not self.check_command("git"):
            log_error("Git not found in PATH")
            return False
            
        # Platform-specific checks
        if self.system == "windows":
            return self._verify_windows_toolchain(env)
        elif self.system == "darwin":
            return self._verify_macos_toolchain(env)
        else:
            return self._verify_linux_toolchain(env)

    def _verify_windows_toolchain(self, env: dict) -> bool:
        """Verify Windows toolchain (MSVC)"""
        log_info("Checking Windows toolchain...")
        
        # Check for Visual Studio
        vs_path = os.environ.get("VSINSTALLDIR") or os.environ.get("VCINSTALLDIR")
        if not vs_path:
            # Try to find via vswhere
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
            if vswhere.exists():
                rc, stdout, _ = self.run_cmd([str(vswhere), "-latest", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"])
                if rc == 0 and stdout.strip():
                    log_success(f"Found Visual Studio at {stdout.strip()}")
                    return True
                    
        if vs_path:
            log_success(f"Found Visual Studio at {vs_path}")
            return True
            
        log_warning("Visual Studio not detected. Ensure 'Desktop development with C++' workload is installed.")
        return True  # Don't fail, might be in Developer Command Prompt

    def _verify_macos_toolchain(self, env: dict) -> bool:
        """Verify macOS toolchain (Xcode/Clang)"""
        log_info("Checking macOS toolchain...")
        
        # Check Xcode command line tools
        rc, _, _ = self.run_cmd(["xcode-select", "-p"])
        if rc != 0:
            log_error("Xcode command line tools not installed. Run: xcode-select --install")
            return False
            
        # Check for ARM64 support
        rc, stdout, _ = self.run_cmd(["clang", "--version"])
        if rc == 0 and "arm64" in platform.machine().lower():
            log_success("Apple Silicon toolchain detected")
            
        return True

    def _verify_linux_toolchain(self, env: dict) -> bool:
        """Verify Linux toolchain"""
        log_info("Checking Linux toolchain...")
        
        # Check for clang or gcc
        has_clang = self.check_command("clang")
        has_gcc = self.check_command("gcc")
        
        if not has_clang and not has_gcc:
            log_error("Neither clang nor gcc found. Install build-essential or clang.")
            return False
            
        # Check for required packages (basic check)
        required = ["pkg-config", "ninja"]
        missing = [cmd for cmd in required if not self.check_command(cmd)]
        if missing:
            log_warning(f"Missing optional tools: {', '.join(missing)}. Install via package manager.")
            
        return True

    def sync_pdfium_source(self, env: dict) -> bool:
        """Sync PDFium source using gclient"""
        log_info("Syncing PDFium source...")
        
        # Create .gclient file
        gclient_content = """
solutions = [
  {
    "name": "pdfium",
    "url": "https://pdfium.googlesource.com/pdfium.git",
    "deps_file": "DEPS",
    "managed": True,
    "custom_deps": {},
    "safesync_url": "",
  },
]
target_os = ["linux", "mac", "win"]
"""
        gclient_path = self.pdfium_dir / ".gclient"
        self.pdfium_dir.mkdir(parents=True, exist_ok=True)
        gclient_path.write_text(gclient_content.strip())
        
        # Run gclient sync
        gclient_cmd = [sys.executable, str(self.depot_tools_dir / "gclient.py"), "sync", "--no-history", "--shallow"]
        rc, stdout, stderr = self.run_cmd(gclient_cmd, cwd=self.pdfium_dir, env=env)
        
        if rc != 0:
            log_error(f"gclient sync failed: {stderr}")
            return False
            
        log_success("PDFium source synced successfully")
        return True

    def run(self) -> bool:
        """Run full bootstrap process"""
        log_info("=" * 60)
        log_info("PDFium Build Environment Bootstrap")
        log_info(f"Platform: {self.system} ({self.machine})")
        log_info("=" * 60)
        
        # Step 1: Install depot_tools
        if not self.install_depot_tools():
            return False
            
        # Step 2: Setup environment
        env = self.setup_environment()
        
        # Step 3: Verify toolchain
        if not self.verify_toolchain(env):
            return False
            
        # Step 4: Sync PDFium source
        if not self.sync_pdfium_source(env):
            return False
            
        log_success("Bootstrap completed successfully!")
        log_info(f"PDFium source available at: {self.pdfium_dir}")
        log_info("Next step: Run build_pdfium.py to compile")
        return True

def main():
    repo_root = Path(__file__).parent.parent
    bootstrap = BootstrapManager(repo_root)
    
    success = bootstrap.run()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()