{
  description = "ld-decode-tools (Nix flake)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgsLegacy.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
    ezpwd = {
      url = "github:pjkundert/ezpwd-reed-solomon";
      flake = false;
    };
  };
  outputs = { self, nixpkgs, nixpkgsLegacy, flake-utils, ezpwd }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgsUnstable = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        legacyPkgs = import nixpkgsLegacy {
          inherit system;
          config.allowUnfree = true;
        };
        ezpwdSrc = ezpwd;
        isLinux = pkgsUnstable.stdenv.hostPlatform.isLinux;
        isDarwin = pkgsUnstable.stdenv.hostPlatform.isDarwin;
        pkgs = if isLinux then legacyPkgs else pkgsUnstable;
        # Vendor older CUDA package sets from legacy nixpkgs for Pascal/GTX 1000 support.
        # Keep both sets available; default to CUDA 11.8 for pre-Volta compatibility.
        vendoredCudaPackages11 = if isLinux then legacyPkgs.cudaPackages_11_8 else null;
        vendoredCudaPackages12 = if isLinux then legacyPkgs.cudaPackages_12_4 else null;
        cudaPackages = vendoredCudaPackages11;
        cudaHostCompiler = if isLinux then legacyPkgs.gcc11 else null;
        cudaCudnnPackage = if isLinux then cudaPackages.cudnn_8_9 else null;
        onnxruntimePackage =
          if isLinux then
            legacyPkgs.stdenvNoCC.mkDerivation rec {
              pname = "onnxruntime-gpu-prebuilt";
              version = "1.18.1";
              src = legacyPkgs.fetchurl {
                url = "https://github.com/microsoft/onnxruntime/releases/download/v${version}/onnxruntime-linux-x64-gpu-${version}.tgz";
                sha256 = "sha256-2UevDkMR/TgBKtad6kmD5zzl8XVNoNW3oRhgPdh7GX0=";
              };
              sourceRoot = "onnxruntime-linux-x64-gpu-${version}";
              dontConfigure = true;
              dontBuild = true;
              installPhase = ''
                runHook preInstall
                mkdir -p "$out"
                cp -r ./* "$out"/
                runHook postInstall
              '';
            }
          else
            pkgs.onnxruntime;
        cudaRuntimeDependencies = pkgs.lib.optionals isLinux [
          cudaPackages.cuda_cudart
          cudaPackages.libcufft
          cudaPackages.libcurand
          cudaPackages.libcublas
          cudaCudnnPackage
        ];
        runtimeLibraryPath = pkgs.lib.optionalString isLinux (pkgs.lib.makeLibraryPath ([ onnxruntimePackage ] ++ cudaRuntimeDependencies));
      in
      let
        packageVersion = "3.0.1";
        rev = if self ? rev then self.rev else "";
        shortRev = if self ? shortRev then self.shortRev else (if rev != "" then builtins.substring 0 7 rev else "unknown");
        dirtySuffix = if self ? dirtyRev then "-dirty" else "";
        branch = if self ? ref then self.ref else "nix";
        nixCommit = "${shortRev}${dirtySuffix}";
      in
      assert pkgs.lib.assertMsg
        (!isLinux || pkgs.lib.versionAtLeast cudaPackages.cudatoolkit.version "11.8")
        "CUDA toolkit must be >= 11.8";
      assert pkgs.lib.assertMsg
        (!isLinux || pkgs.lib.versionAtLeast vendoredCudaPackages12.cudatoolkit.version "12.4")
        "Vendored CUDA 12 package set must provide toolkit >= 12.4";
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "ld-decode-tools";
          version = packageVersion;
          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let
                base = builtins.baseNameOf path;
              in
                !(base == ".git" || base == "build" || base == "result");
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ] ++ pkgs.lib.optionals isLinux [
            cudaPackages.cuda_nvcc
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtsvg
            fftw
            ffmpeg
            sqlite
            libGL
            onnxruntimePackage
          ] ++ pkgs.lib.optionals isLinux [
            cudaPackages.cudatoolkit
            cudaPackages.cuda_cudart
            cudaPackages.libcufft
            cudaPackages.libcurand
            cudaPackages.libcublas
            cudaCudnnPackage
          ];

          cmakeBuildType = "Release";
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DEZPWD_DIR=${ezpwdSrc}/c++"
            "-DAPP_BRANCH=${branch}"
            "-DAPP_COMMIT=${nixCommit}"
          ] ++ pkgs.lib.optionals isDarwin [
            "-DLDCHROMA_ENABLE_CUDA=OFF"
          ] ++ pkgs.lib.optionals isLinux [
            "-DCUDAToolkit_ROOT=${cudaPackages.cudatoolkit}"
            "-DCMAKE_CUDA_HOST_COMPILER=${cudaHostCompiler}/bin/g++"
            "-DONNXRUNTIME_ROOT=${onnxruntimePackage}"
          ];
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.qtbase
            qt6.qtsvg
            fftw
            ffmpeg
            sqlite
            libGL
            python3
            python3Packages.numpy
            onnxruntimePackage
          ] ++ pkgs.lib.optionals isLinux [
            cudaPackages.cudatoolkit
            cudaPackages.cuda_nvcc
            cudaPackages.cuda_cudart
            cudaPackages.libcufft
            cudaPackages.libcurand
            cudaPackages.libcublas
            cudaCudnnPackage
          ];
          EZPWD_DIR = "${ezpwdSrc}/c++";
          ONNXRUNTIME_ROOT = "${onnxruntimePackage}";
          CUDAToolkit_ROOT = pkgs.lib.optionalString isLinux "${cudaPackages.cudatoolkit}";
          CUDA_PATH = pkgs.lib.optionalString isLinux "${cudaPackages.cudatoolkit}";
          shellHook = pkgs.lib.optionalString isLinux ''
            export CUDATOOLKIT_ROOT="$CUDAToolkit_ROOT"
            export CUDAHOSTCXX="${cudaHostCompiler}/bin/g++"
            export CMAKE_CUDA_HOST_COMPILER="${cudaHostCompiler}/bin/g++"
            export LD_LIBRARY_PATH="${runtimeLibraryPath}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          '';
        };
      }
    );
}