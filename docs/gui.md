## Main Window
TitleBar
* label: Command Runner
* with icon

Toolbar:
* left align
* button with label only
* Buttons: (A)dd, (E)dit, (D)elete, (S)tart, S(t)op, (R)estart

Split Resizer:
* Top
  * Grid
    * Columns: Name, Status, PID, Exit Code, Auto Start, Working Directory
* Bottom:
  * Action Bar
    * Left Align: 
      * Radio Group: Co(m)bined, stdo(u)t, std(e)rr
    * Right Align:
      * Button: (C)lear
      * Button: (J)ump to Latest
      * Checkbox: (W)ord wrap
      * Checkbox (selected by default): Auto-scro(l)l
  * label: Log output
  * Readonly Multiline textbox: log content
  
## Command Dialog
TitleBar:
* Label: "Add Command" or "Edit Command"
* Dialog Window can resize

Form(label with fixed width, textbox fill with window width):
* (N)ame: TextBox
* (W)orking Directory: Directory Selector
* (O)output Encoding: Dropdown: auto, gbk, utf-8, system
* CheckBoxs: S(h)ell, (A)uto Start
* Command (Line): Multiline textbox

Buttons: 
* (S)ave, (C)ancel
* right align

