{
  description = "An extremely fast and simple dmenu/rofi replacement for Wayland";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?rev=0ad6f47ea4fe188f4bc8f0380f93ae8523337c6c"; # nixos-26.05 (10 jul 2026)
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: import nixpkgs { inherit system; };
      mkTofi = pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "tofi";
          version = "0.9.1";
          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let
                name = baseNameOf path;
              in
              pkgs.lib.cleanSourceFilter path type
              && name != "result"
              && !(type == "directory" && builtins.elem name [ "build" ".cache" ]);
          };

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            scdoc
            wayland-protocols
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            cairo
            freetype
            glib
            harfbuzz
            libxkbcommon
            pango
            wayland
          ] ++ nixpkgs.lib.optionals pkgs.stdenv.hostPlatform.isMusl [
            pkgs.libfts
          ];

          doCheck = true;

          meta = with pkgs.lib; {
            description = "Fast and simple dmenu/rofi replacement for Wayland";
            homepage = "https://github.com/philj56/tofi";
            license = licenses.mit;
            mainProgram = "tofi";
            platforms = platforms.linux;
          };
        };
    in
    {
      packages = forAllSystems (system:
        let
          tofi = mkTofi (pkgsFor system);
        in
        {
          inherit tofi;
          package = tofi;
          default = tofi;
        });

      devShells = forAllSystems (system:
        let
          pkgs = pkgsFor system;
          package = mkTofi pkgs;
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ package ];
            packages = with pkgs; [
              gdb
            ];
          };
        });
    };
}
