# cnc-ra-libs — Shared engine libraries for Command & Conquer

Shared, game-agnostic libraries used by both Tiberian Dawn and Red Alert engine ports.

## License

This is a derivative work based on the Command & Conquer and Red Alert source code
released by Electronic Arts under the GNU General Public License v3.0.

Original source: [CnC_Remastered_Collection](https://github.com/electronicarts/CnC_Remastered_Collection) (EA, GPL-3.0)

See [LICENSE](LICENSE) for the full license text.

## Libraries

| Directory | Description |
|-----------|-------------|
| `audio/`    | Audio playback (AUD/VQA sound) |
| `compat/`   | Platform compatibility shims (Win32 → POSIX) |
| `crypto/`   | MIX file reading with Blowfish/RSA crypto |
| `graphics/` | SHP/WSA sprite codecs (LCW, XOR delta) |
| `math/`     | Fixed-point and trigonometry utilities |
| `render/`   | Rendering infrastructure (sprite providers, atlas, compositor, GL ES backend) |
| `vqa/`      | VQA video decoder |
