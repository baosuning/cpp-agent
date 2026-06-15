---
name: get_cwd
description: Gets the current working directory path
---

# Get Current Working Directory

To get the current working directory, use the `execute_command` tool with the appropriate command for your operating system:

- **Windows**: Run `cd` command (no arguments)
- **Linux/Mac**: Run `pwd` command

## Output

The current working directory path as a string.

## Example

If the current directory is `D:\codes\project`, the output would be:
```
D:\codes\project
```