# XMILE Test Suite

This is a repository of xmile models & their output.

## Vendor-specific model files

There is currently no publically available software that reads,
simulates & writes the current version of the
[XMILE](https://www.oasis-open.org/committees/tc_home.php?wg_abbrev=xmile)
specification.  It is preferrable to add vendor-specific files to this
repository, and once the the specification is finalized we can convert
from these vendor-specific files into XMILE.

## Adding a new model with STELLA/iThink results

This is expected to be the most common method for now, even though the
XML files STELLA uses do not match the XMILE specification.  The
following is the manual process that should be followed to add a model
to this repository.

1. Sign up for a github account.
2. Fork this repository on github
3. Clone the repository onto your machine using git or the github desktop application.
4. Create a folder for your model.  The folder name should be
   descriptive of the model and contain no spaces - underscores &
   dashes are ok.
5. Add your model to the folder you just created.  The model name
   should be `model.itmx` or `model.stmx`
6. Open the model in STELLA or iThink
7. Run the model (choose Run from the Run menu)
8. From the Edit menu, choose Export Data
9. In the Export Data modal dialog, choose One Time as the Export Type
10. For Data Source, choose to Export All Model Variables & Every DT
11. For Export Destination, choose Browse and name the file
    `data.csv`, and make sure the left-most checkbox below Browse is
    selected.
12. Finally, add a file called `metadata.json` which contains
    information about the version of STELLA/iThink the results were
    obtained from, like:

```javascript
{
    "vendor": "isee STELLA v10.0.5"
}
```

13. Commit the 3 files (`model.{itmx,stmx}`, `data.csv`,
    `metadata.json` in the folder you added, push it to github, and
    open a pull request.

