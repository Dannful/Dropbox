{
  description = "Nix flake file for a Dropbox project";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachSystem [ "x86_64-linux" ] (system:
      let
        pkgs = import nixpkgs { inherit system; };
        stdenv = pkgs.gcc14Stdenv;
      in {
        devShells.default = pkgs.mkShell.override { inherit stdenv; } {
          buildInputs = with pkgs; [ openssl ];
          packages = with pkgs; [ llvmPackages_17.clang-tools ];
          env = {
            CLANGD_FLAGS = "--query-driver=${pkgs.lib.getExe stdenv.cc}";
          };
        };
      });
}
