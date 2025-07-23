{ inputs, ... }: {
  imports = [
    inputs.doom-wasm-flake.flakeModule
    inputs.hydra-coding-standards.flakeModule
  ];

  perSystem = _: {
    coding.standards.hydra.enable = true;
    doom."default" = {
      src = inputs.self;
    };
  };
}
