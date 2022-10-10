# [U25N-SmartNIC-Solution Documentation](https://xilinx.github.io/U25N-SmartNIC-Solution/)

This branch [gh-pages](https://github.com/Xilinx/U25N-SmartNIC-Solution/tree/gh-pages) is for UG1534 U25N SmartNIC User Guide.

Before updating the document, please install below python modules. 
```bash
pip3 install recommonmark sphinx sphinx_markdown_tables rst2pdf
```

To update the document, please follow below steps.
- Update document `version` and `release` in docs/conf.py
    ```text
    # The short X.Y version
    version = '1.3'
    # The full version, including alpha/beta/rc tags
    release = '1.3'
    ```
- Update date in docs/conf.py
    ```text
    html_last_updated_fmt = 'Sept 1, 2022'
    ```
- Update document's markdown source files in docs/source/docs/*.md
- Generate html and pdf files
    ```bash
    cd U25N-SmartNIC-Solution/docs/
    make clean
    make html
    make pdf
    ```