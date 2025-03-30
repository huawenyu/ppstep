# ppstep
The C and C++ preprocessors are famous for allowing users to write opaque, hard-to-follow code through macros. These downsides are tolerated because macros also allow code to have a high level of abstraction, which can reduce software complexity and improve developer productivity. Doing the good parts takes dicipline; eliminating the bad parts takes refactoring, and importantly, debugging. PPstep is the debugger that macro writers can use to do that.

<p align="center">
  <i>preprocessing a sequence into a tuple, visualized!</i>
</p>

## Features
- Visually single-step through macro expansion and rescanning until preprocessing is complete
- Set breakpoints on macros for specific preprocessing events, and continue preprocessing between them
- Show backtrace of pending macro expansions, and forward-trace of future macro rescans
- #define/#undef macros mid-preprocessing, and interactively expand macros at any time
- **TODO:** Reverse stepping to rewind preprocssing and view steps from an earlier point
- **TODO:** visualizing #if/#elif/#else branches to explore conditional compilation

## Building

```sh
 ### Install dependencies
sudo apt install build-essential 
sudo apt install libboost-all-dev
sudo apt install cmake

 ### build from source
git clone https://github.com/huawenyu/ppstep.git
cd ppstep
mkdir build && cd build && cmake .. && make
```

## Usage

To try it out, run `ppstep your-source-file.c`. `ppstep` supports common preprocessor flags like:
```sh
  --include/-I   add include directories,
  --define/-D    define macros,
  --undefine/-U  undefine macros,

Usage: rlwrap [-options] -z ./filter.py <command>

Source:
    https://github.com/notfoundry/ppstep
    /nix/store/238vrcj4b0fcp0yivxcp7mzl1m9jrmkk-rlwrap-0.46.1/share/rlwrap/filters/rlwrapfilter.py

Pre-requirement:
    clang-format: nix-env -iA nixpkgs.clang-tools

pp-shell:
prompt: pp>
commands:
    q-quit
    s-step
    c-continue

    bt-backtrace
    ft-forwardtrace
    b-break: break call <macro>, break expand <macro>
    d-delete: delete call <macro>
process-status:
    called,exanded,rescanned,lexed

```

#### The Prompt

You should see a prompt that looks like `pp>`. 
- From here, you can step forward through preprocessing steps using the `step` or `s` commands, and see visually what each step does.  
- You will notice that the prompt will have a suffix added to it to show what the current preprocessing step is,  
  such as `called`, `expanded`, `rescanned`, or `lexed`.  
- Newly-encountered macro calls, finished macro expansions, and finished macro rescans are each color-coded in the visual output so you can see where changes were made.  
- When you are done, you can use the `quit` or `q` commands to exit the prompt.

- While stepping, if you want to see the history of pending macro expansions, you can use the `backtrace` or `bt` commands.  
- You can also look into the future to see what the anticipated macro rescans will be by using the `forwardtrace` or `ft` commands.

#### Breakpoints
- If there is a specific macro and preprocessing step that you are interested in visualizing, you can set a breakpoint on that macro using the `break` or `b` commands.
- To break when a specific macro is called, for example, you could enter `break call YOUR_MACRO` or `bc YOUR MACRO`.
- Similarly to break when that macro is finished expanding, you could enter `break expand YOUR_MACRO` or `be YOUR_MACRO`.
- To continue preprocessing until one of these breakpoints is hit (or preprocessing is finished), use the `continue` or `c` commands.  
- Deleting a breakpoint has a similar syntax to setting them: the complements to `break call YOUR_MACRO` or `bc YOUR_MACRO` are `delete call YOUR_MACRO` or `dc YOUR_MACRO`.

#### Interactive Evaluation

If you choose to, you can also use preprocessor directives mid-preprocessing.  
For example, you could say `#define NEW_MACRO(x) x` to create a function-like macro named `NEW_MACRO` in real-time. `#include` and `#undef` also work as expected (though undefining a macro in the process of being expanded without then re-defining another macro under that name can have terrible consequences!) Macros can also be expanded mid-preprocessing with the `expand` or `e` commands.
For example, `expand NEW_MACRO(1)` would open a nested prompt allowing you to step through each of the expansion stages of `NEW_MACRO`.
