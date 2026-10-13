namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>View-only address transition phases; browser/navigation state remains in the ViewModel.</summary>
    internal enum AddressEditingPhase
    {
        /// <summary>Compact browsing chrome with no edit surface.</summary>
        Hidden,
        /// <summary>Opaque edit surface is appearing before input geometry changes.</summary>
        Revealing,
        /// <summary>Input expands before native focus is requested.</summary>
        Opening,
        /// <summary>Native TextBox focus and keyboard own input interaction.</summary>
        Editing,
        /// <summary>Editor remains visible while Windows hides the keyboard.</summary>
        HidingKeyboard,
        /// <summary>Input shrinks to compact geometry with its background retained.</summary>
        Closing,
        /// <summary>Edit surface fades while all compact controls restore together.</summary>
        Fading
    }
}
