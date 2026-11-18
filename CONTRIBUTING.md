# Contributing

Thank you for your interest in contributing to the development of wfview! wfview is the product of many people's hard work, and we hope you will join us! 

## General Guidelines

If you wish to make a source-code change to wfview: 

1. Make sure you have tried your change on the latest main branch code. 
2. Test the code on as many radios and platforms as you can (and mention this in your PR). Run the github workflow to verify it compiles on each test platform. 
3. Attribute any code you're using from other sources, including any licenses, if applicable. 
4. If you used AI, follow the AI guidelines below. 
5. Create a github account. 
6. Create an [Issue](https://github.com/wf-group/wfview/issues), so that we may expect your PR. This is also a chance for the wfview team to see if it might be an issue we have already addressed. Write your issue (and PR) to be read by a human.
7. Please limit the rate of issue and PR creation. Combine similar issues and PRs together by theme or section of code. We human-audit all submitted code, so be aware of the limitations of our time. 
8. If your Issue includes changes to the appearance of the application, show some screenshots to demonstrate what you're considering.
9. Update the language translations if possible. wfview includes [a tool](tools/wfview_translate.py) for this which uses a google translate API to run. The Google Translate API is basically free of charge for small works (ie, under 500,000 words). The tool can be run to only update the untranslated words, in bulk, for each language. 
10. After creating the Issue, and optionally after a day of delay for contemplation, [open the PR](https://github.com/wf-group/wfview/pulls). 

## Coding Philosophy 

1. Rig agnostic: we want to keep the vast majority of wfview free from manufacturer-specific or rig-specific code branching. If you discover something within the main bulk of wfview that needs to work differently for a radio, see about adding that difference to rigCaps and then into either the manufacturer-specific driver or into the rig files (and also into Rig Creator).
2. Sensitivities: wfview contains code that was fought hard for. Please keep that in mind. Don't change code that you don't understand or cannot test. "Touch nothing but the lamp" is your guidance here. 
3. Works for everyone: We want the experience of wfview to be quite similar for all operating systems, desktop environments, and radios. Try and use Qt's built-in OS-agnostic helpers for things like file paths. Exceptions include OS-specific font, color, and file handling dialogs, for example. 
4. MCP: The MCP server is your built-in runtime path into the guts of wfview for an agent to help you debug and diagnose issues. Make good use of it! Examine the timing, errors, cache state, and so on while running. If you add a feature that could use some monitoring during runtime, consider an MCP hook. MCP is a compile-time option, and to the extent possible, your MCP additions should be as well. 
5. UI: We are striving for a good balance between clutter and feature presence. This is by no means perfect as it stands, but keep it in mind. Not every feature belongs on the main UI, but clearly, some do. Make sure to check things like resizing. The custom macro buttons also can enable a user to add features later, so keep in mind that it may be appropriate to simply add the capability to wfview and have it as an available command. We also want to make sure the UI stays "snappy", so benchmark your UI refresh rates and so on if you have added anything which might alter it. 
6. UI Controls: Clearly label what controls do. Add tooltips (with appropriate tooltip sizing). Balance the size of the control against the overall UI appearance.
7. Logging: Log what's going on, especially if it misbehaves. Keep the logging quiet for the default mode of operation. Use debug for debug (ie, looking at what is happening while nominal), warn for warnings when things aren't operating as we expect, critical for warnings when we're gonna crash or misbehave, and info when you feel the need to inform the user about something.
8. Settings file: Try and keep the settings as backward-compatible as possible. We also want to be sure to not crash if an empty or malformed settings file is loaded up, so do testing and sanitize if needed.

## Build System

Make good use of the built-in build system (github workflow). This is a quick way to verify each platform compiles. You may wish to turn off the build system for your fork and merely run it once a day, for example. Our build system is not perfect but it does work. We welcome input but will reject them when we're near shipping time for a new version. 

## Use of AI

The wfview team recognizes the tremendous value AI brings to software development. You are welcome to use AI tools to navigate the codebase, understand how components work, or even generate the code you submit.

However, you -- the human contributor -- are **solely responsible** for your submission. Be cognizant of what you are submitting. You must have read, tested, and understood all the code you submit, and, you should be able to explain how it works. When opening a pull request, please disclose:
- AI Tool / Model: (e.g., GitHub Copilot, Cursor, Claude Opus 5, Gemini Helium, etc)
- How it was used: (e.g., boilerplate generation, debugging, refactoring, vibe, etc)

## Licensing and Copyright 

wfview is copyright © 2018–2026 Elliott H. Liggett and Phil E. Taylor, all rights reserved. The wfview codebase is licensed under the GNU General Public License v3.0 (GPLv3).

By submitting a contribution to this repository, you agree that:

- You created the contribution yourself (or have the legal right to submit it) and are transferring copyright ownership of the changes to Elliott H. Liggett and Phil E. Taylor.
- Your contribution will be publicly distributed under the terms of the GNU GPLv3 license.
