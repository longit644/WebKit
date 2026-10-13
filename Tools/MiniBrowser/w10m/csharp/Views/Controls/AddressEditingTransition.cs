using System;
using System.Diagnostics;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media.Animation;

namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Owns reveal/morph/focus and reverse closing choreography for one page's address chrome.</summary>
    internal sealed class AddressEditingTransition
    {
        /// <summary>The view whose input, background and compact controls move together.</summary>
        private readonly AddressBarControl addressBar;

        /// <summary>The opaque edit surface that must survive native keyboard dismissal.</summary>
        private readonly Button surface;

        /// <summary>Native keyboard visibility owner.</summary>
        private readonly InputPane inputPane;

        /// <summary>Checks the page's temporary loaded lifetime before a delayed view stage.</summary>
        private readonly Func<bool> isActive;

        /// <summary>Reads the keyboard rectangle announced by the page's native event source.</summary>
        private readonly Func<bool> keyboardVisible;

        /// <summary>Moves focus to the owning page before asking Windows to hide the keyboard.</summary>
        private readonly Action releaseFocus;

        /// <summary>Current independently running surface/compact-control animation.</summary>
        private Storyboard animation;

        /// <summary>One-shot next stage, stopped when another request supersedes it.</summary>
        private DispatcherTimer stageTimer;

        /// <summary>Request identity rejecting obsolete timer/storyboard completions.</summary>
        private int generation;

        /// <summary>Logical edit request, distinct from the view phase retained during closing.</summary>
        private bool requestedEditing;

        /// <summary>Monotonic end of the native Hiding settling interval.</summary>
        private long nativeKeyboardMotionEnd;

        /// <summary>Gets the current view phase.</summary>
        internal AddressEditingPhase Phase { get; private set; }

        /// <summary>Reports phase boundaries for optional diagnostics without controlling browser state.</summary>
        internal event Action<AddressEditingPhase> PhaseChanged;

        /// <summary>Creates one view owner; it subscribes to no process/window events itself.</summary>
        /// <param name="addressBar">The page-owned address control.</param>
        /// <param name="surface">The edit surface above web content.</param>
        /// <param name="inputPane">Native keyboard service.</param>
        /// <param name="isActive">Temporary page lifetime predicate.</param>
        /// <param name="keyboardVisible">Current announced keyboard visibility.</param>
        /// <param name="releaseFocus">Owner-page focus action.</param>
        internal AddressEditingTransition(AddressBarControl addressBar, Button surface, InputPane inputPane,
            Func<bool> isActive, Func<bool> keyboardVisible, Action releaseFocus)
        {
            this.addressBar = addressBar;
            this.surface = surface;
            this.inputPane = inputPane;
            this.isActive = isActive;
            this.keyboardVisible = keyboardVisible;
            this.releaseFocus = releaseFocus;
        }

        /// <summary>Gets whether independent reposition motion must still defer to Windows keyboard movement.</summary>
        internal bool KeyboardMotionInProgress
        {
            get { return Stopwatch.GetTimestamp() < nativeKeyboardMotionEnd; }
        }

        /// <summary>Supersedes previous view work and starts the current logical edit request.</summary>
        /// <param name="editing">Whether to reveal/focus or reverse-close the editor.</param>
        internal void Request(bool editing)
        {
            int request = ++generation;
            requestedEditing = editing;
            double initialOpacity = surface.Visibility == Visibility.Visible ? surface.Opacity : 0;
            StopPending();
            if (!isActive())
            {
                ResetVisuals();
                return;
            }
            if (editing)
                Open(request, initialOpacity);
            else
                Close(request);
        }

        /// <summary>Records native Hiding before the ViewModel may request close in the same callback.</summary>
        internal void KeyboardHiding()
        {
            nativeKeyboardMotionEnd = Stopwatch.GetTimestamp()
                + (long)(Stopwatch.Frequency * BrowserChromeTiming.KeyboardHideSettleMilliseconds / 1000);
            if (!requestedEditing && Phase == AddressEditingPhase.HidingKeyboard)
                ScheduleClose(generation);
        }

        /// <summary>Cancels all callbacks on temporary unload and restores a clean compact baseline.</summary>
        internal void Reset()
        {
            ++generation;
            requestedEditing = false;
            StopPending();
            ResetVisuals();
        }

        /// <summary>Reveals the surface, then morphs input and only afterward grants native focus.</summary>
        /// <param name="request">Current generation.</param>
        /// <param name="initialOpacity">Current surface opacity, including interrupted closing.</param>
        private void Open(int request, double initialOpacity)
        {
            surface.Visibility = Visibility.Visible;
            surface.Opacity = initialOpacity;
            addressBar.SetCompactControlsOpacity(0);
            SetPhase(AddressEditingPhase.Revealing);
            Storyboard reveal = new Storyboard();
            AddOpacity(reveal, surface, initialOpacity, 1);
            reveal.Completed += (sender, args) =>
            {
                if (!IsCurrent(request, true))
                    return;
                reveal.Stop();
                animation = null;
                surface.Opacity = 1;
                addressBar.IsAddressEditing = true;
                SetPhase(AddressEditingPhase.Opening);
                Schedule(request, BrowserChromeTiming.AddressMorphMilliseconds, () =>
                {
                    if (IsCurrent(request, true))
                    {
                        SetPhase(AddressEditingPhase.Editing);
                        addressBar.AddressBox.Focus(FocusState.Programmatic);
                    }
                });
            };
            animation = reveal;
            reveal.Begin();
        }

        /// <summary>Keeps edit visuals until keyboard settling, then begins the compact morph.</summary>
        /// <param name="request">Current generation.</param>
        private void Close(int request)
        {
            if (surface.Visibility != Visibility.Visible)
            {
                ResetVisuals();
                return;
            }
            bool awaitingKeyboard = keyboardVisible();
            if (awaitingKeyboard || KeyboardMotionInProgress)
                SetPhase(AddressEditingPhase.HidingKeyboard);
            if (addressBar.AddressBox.FocusState != FocusState.Unfocused)
                releaseFocus();
            inputPane.TryHide();
            if (!awaitingKeyboard || KeyboardMotionInProgress)
                ScheduleClose(request);
            else
            {
                // A canceled opening can have no Hiding event; never infer closure while occlusion remains.
                Schedule(request, BrowserChromeTiming.KeyboardHideSettleMilliseconds, () =>
                {
                    if (!keyboardVisible())
                        ScheduleClose(request);
                });
            }
        }

        /// <summary>Shares the native deadline rather than appending another full settling interval.</summary>
        /// <param name="request">Current generation.</param>
        private void ScheduleClose(int request)
        {
            double remaining = Math.Max(0, (nativeKeyboardMotionEnd - Stopwatch.GetTimestamp())
                * 1000.0 / Stopwatch.Frequency);
            Schedule(request, remaining, () => MorphClosed(request));
        }

        /// <summary>Restores final compact geometry before fading the surface and controls together.</summary>
        /// <param name="request">Current generation.</param>
        private void MorphClosed(int request)
        {
            if (!IsCurrent(request, false))
                return;
            bool hadEditor = addressBar.IsAddressEditing;
            addressBar.IsAddressEditing = false;
            addressBar.SetTransitionBackground(hadEditor);
            SetPhase(AddressEditingPhase.Closing);
            Schedule(request, hadEditor ? BrowserChromeTiming.AddressMorphMilliseconds : 0, () => FadeClosed(request));
        }

        /// <summary>Restores splitter and buttons through one compact group, avoiding isolated opacity toggles.</summary>
        /// <param name="request">Current generation.</param>
        private void FadeClosed(int request)
        {
            if (!IsCurrent(request, false))
                return;
            addressBar.SetTransitionBackground(false);
            SetPhase(AddressEditingPhase.Fading);
            Storyboard fade = new Storyboard();
            AddOpacity(fade, surface, surface.Opacity, 0);
            foreach (FrameworkElement compact in addressBar.CompactControlElements)
                AddOpacity(fade, compact, compact.Opacity, 1);
            fade.Completed += (sender, args) =>
            {
                if (!IsCurrent(request, false))
                    return;
                fade.Stop();
                animation = null;
                ResetVisuals();
            };
            animation = fade;
            fade.Begin();
        }

        /// <summary>Tests request identity and page lifetime before delayed callbacks access the view.</summary>
        /// <param name="request">Callback generation.</param>
        /// <param name="editing">Expected logical request.</param>
        /// <returns>Whether the callback still owns current view work.</returns>
        private bool IsCurrent(int request, bool editing)
        {
            return request == generation && requestedEditing == editing && isActive();
        }

        /// <summary>Runs one cancelable stage on the existing owner dispatcher.</summary>
        /// <param name="request">Current generation.</param>
        /// <param name="milliseconds">Delay until the next view stage.</param>
        /// <param name="stage">The view-only stage.</param>
        private void Schedule(int request, double milliseconds, Action stage)
        {
            if (stageTimer != null)
                stageTimer.Stop();
            DispatcherTimer timer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(Math.Max(1, milliseconds))
            };
            timer.Tick += (sender, args) =>
            {
                timer.Stop();
                if (stageTimer == timer)
                    stageTimer = null;
                if (request == generation && isActive())
                    stage();
            };
            stageTimer = timer;
            timer.Start();
        }

        /// <summary>Stops superseded timelines without permitting their completion to restore a newer request.</summary>
        private void StopPending()
        {
            if (animation != null)
            {
                animation.Stop();
                animation = null;
            }
            if (stageTimer != null)
            {
                stageTimer.Stop();
                stageTimer = null;
            }
        }

        /// <summary>Restores compact controls after fade or immediate lifetime cancellation.</summary>
        private void ResetVisuals()
        {
            addressBar.IsAddressEditing = false;
            addressBar.SetCompactControlsOpacity(1);
            surface.Opacity = 0;
            surface.Visibility = Visibility.Collapsed;
            SetPhase(AddressEditingPhase.Hidden);
        }

        /// <summary>Publishes a view phase only when it actually changes.</summary>
        /// <param name="phase">New phase.</param>
        private void SetPhase(AddressEditingPhase phase)
        {
            if (Phase == phase)
                return;
            Phase = phase;
            PhaseChanged?.Invoke(phase);
        }

        /// <summary>Adds one independent opacity track using the shared surface timing.</summary>
        /// <param name="transition">Surface/compact-control transition.</param>
        /// <param name="target">View element whose opacity changes.</param>
        /// <param name="from">Current effective opacity.</param>
        /// <param name="to">Final opacity.</param>
        private static void AddOpacity(Storyboard transition, UIElement target, double from, double to)
        {
            DoubleAnimation opacity = new DoubleAnimation
            {
                From = from,
                To = to,
                Duration = new Duration(TimeSpan.FromMilliseconds(BrowserChromeTiming.EditSurfaceMilliseconds)),
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
            };
            Storyboard.SetTarget(opacity, target);
            Storyboard.SetTargetProperty(opacity, "Opacity");
            transition.Children.Add(opacity);
        }
    }
}
