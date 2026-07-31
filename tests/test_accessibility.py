import pytest

from command_runner.ui.accessibility import add_control_mnemonic, add_label_mnemonic


class FakeWindow:
    def __init__(self):
        self.bindings = {}

    def bind(self, sequence, callback, add=None):
        self.bindings[sequence] = (callback, add)


class FakeWidget:
    def __init__(self):
        self.options = {}
        self.invocations = 0
        self.focused = False

    def __setitem__(self, key, value):
        self.options[key] = value

    def invoke(self):
        self.invocations += 1

    def focus_set(self):
        self.focused = True


def test_control_mnemonic_underlines_and_invokes():
    window = FakeWindow()
    control = FakeWidget()

    add_control_mnemonic(window, control, "Save", "S")

    assert control.options["underline"] == 0
    result = window.bindings["<Alt-s>"][0]()
    assert control.invocations == 1
    assert result == "break"
    assert window.bindings["<Alt-s>"][1] == "+"
    assert "<Alt-S>" in window.bindings


def test_label_mnemonic_underlines_and_focuses_target():
    window = FakeWindow()
    label = FakeWidget()
    field = FakeWidget()

    add_label_mnemonic(window, label, "Output Encoding", "O", field)

    assert label.options["underline"] == 0
    window.bindings["<Alt-o>"][0]()
    assert field.focused


def test_mnemonic_must_be_part_of_text():
    with pytest.raises(ValueError):
        add_control_mnemonic(FakeWindow(), FakeWidget(), "Clear", "K")
