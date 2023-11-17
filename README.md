THIS BRANCH IS DEAD!


But just in case you noticed that there's more globals, you're right:
```bash
rg '^static [^) ,]+ \(?[^, ]+\)?;$' -g '*.cxx' -g '!odk/' -g '!hwpfilter/' -g '!**/qa' -g '!compilerplugins/' -g '!onlineupdate/' -g '!basic/' -g '!java/' -g '!vcl/workben/' -g '!sc/' -g '!desktop/source/lib/lokandroid.cxx' -g '!test/' -g '!pyuno/' -g '!vcl/unx/gtk3/' -g '!jvmfwk/' -g '!libreofficekit/source/gtk/' -g '!connectivity/' -g '!vcl/backendtest' -g '!vcl/skia/'
```
