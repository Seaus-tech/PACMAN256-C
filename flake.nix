{
  description = "Pac-Man 256 C game";

  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = nixpkgs.legacyPackages.${system};
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            cmake
            clang
            llvmPackages.clang-tools
          ] ++ lib.optionals stdenv.isDarwin [
            darwin.apple_sdk.frameworks.Cocoa
            darwin.apple_sdk.frameworks.Metal
            darwin.apple_sdk.frameworks.QuartzCore
          ] ++ lib.optionals stdenv.isLinux [
            xorg.libX11
            libGL
          ];
        };
      }
    );
}
