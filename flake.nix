{
  description = "doom-wasm";

  inputs = {
    doom-wasm-flake.url = "github:cardano-scaling/doom-wasm-flake/0.3.1";
    flake-parts.url = "github:hercules-ci/flake-parts";
    hydra-coding-standards.url = "github:cardano-scaling/hydra-coding-standards/0.7.0";
    import-tree.url = "github:vic/import-tree";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-23.11";
  };

  outputs = inputs: inputs.flake-parts.lib.mkFlake { inherit inputs; } (inputs.import-tree ./nix);

}
