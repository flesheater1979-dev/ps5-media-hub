# samples/

Scratch space for experiments and reference material that is not a first-class
project. It is empty on purpose (only `.gitkeep`).

The PS5 Payload SDK's own samples are in the container at
`$PS5_PAYLOAD_SDK/samples/` and are the authoritative examples of how the SDK
expects to be used (`hello_world`, `hello_sprx` for loading Sony modules,
`install_app` for the fake-SELF plus `param.json` shape, `hwinfo`, `notify`, ...).
Build one out-of-tree so the SDK volume stays clean:

```bash
cp -r $PS5_PAYLOAD_SDK/samples/hello_stdio /tmp/x && make -C /tmp/x
```

EVO itself is the `PPSA99039` app module, not an SDK payload; see
[`../docs/architecture/app-module.md`](../docs/architecture/app-module.md).
