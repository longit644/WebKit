using System;
using System.Diagnostics;
using Windows.Foundation;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Media.Animation;

namespace WebKitMiniBrowser.Views.Controls
{
    /// <summary>Animates chrome from its previous visual bounds to its newly arranged layout.</summary>
    internal sealed class ChromeLayoutTransition
    {
        /// <summary>Logical-pixel tolerance for roundoff in animated coordinate transforms.</summary>
        private const double GeometryTolerance = 0.01;

        /// <summary>The chrome element whose layout is already owned by XAML.</summary>
        private readonly FrameworkElement element;

        /// <summary>Stable ancestor used to compare arranged positions.</summary>
        private readonly FrameworkElement coordinateSpace;

        /// <summary>Whether the visual is a background that may scale without stretching text.</summary>
        private readonly bool animateSize;

        /// <summary>Time budget for this visual's movement, in milliseconds.</summary>
        private readonly double transitionMilliseconds;

        /// <summary>System setting controlling optional motion.</summary>
        private readonly UISettings settings = new UISettings();

        /// <summary>Render-only correction preserving the prior visual after layout changes.</summary>
        private readonly CompositeTransform transform = new CompositeTransform();

        /// <summary>The current independently running transform animation.</summary>
        private Storyboard animation;

        /// <summary>The last final layout rectangle, without the render-only correction.</summary>
        private Rect previousBounds;

        /// <summary>The ancestor width used to reset motion across orientation/width changes.</summary>
        private double previousCoordinateWidth;

        /// <summary>Whether a valid arranged baseline has been captured.</summary>
        private bool hasPreviousBounds;

        /// <summary>Whether the element's layout event is subscribed.</summary>
        private bool subscribed;

        /// <summary>Shared end time for geometry updates belonging to the current system transition.</summary>
        private double transitionDeadline;

        /// <summary>Whether a system/state event prepared the next arrangement's motion deadline.</summary>
        private bool transitionPrepared;

        /// <summary>Attaches view-only movement to an element with no other render transform.</summary>
        /// <param name="element">The address chrome, input, or input background.</param>
        /// <param name="coordinateSpace">An ancestor unaffected by this element's transform.</param>
        /// <param name="animateSize">True only for a background, never for text or the web surface.</param>
        /// <param name="transitionMilliseconds">Motion budget, anchored to the originating event.</param>
        internal ChromeLayoutTransition(FrameworkElement element, FrameworkElement coordinateSpace,
            bool animateSize = false,
            double transitionMilliseconds = BrowserChromeTiming.RepositionMilliseconds)
        {
            this.element = element;
            this.coordinateSpace = coordinateSpace;
            this.animateSize = animateSize;
            this.transitionMilliseconds = transitionMilliseconds;
            element.RenderTransformOrigin = new Point();
            element.RenderTransform = transform;
            element.Loaded += OnLoaded;
            element.Unloaded += OnUnloaded;
        }

        /// <summary>Discards obsolete motion so the next arrangement establishes a fresh baseline.</summary>
        internal void Reset()
        {
            StopAnimation();
            hasPreviousBounds = false;
            transitionPrepared = false;
            transitionDeadline = 0;
        }

        /// <summary>Gets whether geometry updates can still share the current event's motion deadline.</summary>
        internal bool IsTransitionInProgress
        {
            get { return MillisecondsNow() < transitionDeadline; }
        }

        /// <summary>Allows a native system animation to own movement without an additional chrome tween.</summary>
        internal bool MotionEnabled { get; set; } = true;

        /// <summary>Anchors motion to the system/state notification before asynchronous layout runs.</summary>
        internal void PrepareTransition()
        {
            transitionDeadline = MillisecondsNow() + transitionMilliseconds;
            transitionPrepared = true;
        }

        /// <summary>Starts observing naturally completed layout without forcing UpdateLayout.</summary>
        /// <param name="sender">The loaded chrome element.</param>
        /// <param name="args">The loaded event.</param>
        private void OnLoaded(object sender, RoutedEventArgs args)
        {
            Reset();
            if (!subscribed)
            {
                element.LayoutUpdated += OnLayoutUpdated;
                subscribed = true;
            }
        }

        /// <summary>Revokes layout observation and cancels motion on temporary unload.</summary>
        /// <param name="sender">The unloaded chrome element.</param>
        /// <param name="args">The unloaded event.</param>
        private void OnUnloaded(object sender, RoutedEventArgs args)
        {
            if (subscribed)
            {
                element.LayoutUpdated -= OnLayoutUpdated;
                subscribed = false;
            }
            Reset();
        }

        /// <summary>Preserves the current visual and animates only render properties to final layout.</summary>
        /// <param name="sender">The XAML layout event source.</param>
        /// <param name="args">The completed layout notification.</param>
        private void OnLayoutUpdated(object sender, object args)
        {
            if (element.ActualWidth <= 0 || element.ActualHeight <= 0)
            {
                Reset();
                return;
            }

            Point origin = element.TransformToVisual(coordinateSpace).TransformPoint(new Point());
            Rect bounds = new Rect(origin.X - transform.TranslateX, origin.Y - transform.TranslateY,
                element.ActualWidth, element.ActualHeight);
            double coordinateWidth = coordinateSpace.ActualWidth;
            if (hasPreviousBounds && SameBounds(previousBounds, bounds))
                return;

            Rect visualBounds = new Rect(previousBounds.X + transform.TranslateX,
                previousBounds.Y + transform.TranslateY,
                previousBounds.Width * transform.ScaleX, previousBounds.Height * transform.ScaleY);
            double now = MillisecondsNow();
            if (!transitionPrepared && animation == null)
                transitionDeadline = now + transitionMilliseconds;
            double remainingMilliseconds = Math.Max(0, transitionDeadline - now);
            transitionPrepared = false;
            bool shouldAnimate = hasPreviousBounds && MotionEnabled && settings.AnimationsEnabled
                && remainingMilliseconds > 0
                && Math.Abs(previousCoordinateWidth - coordinateWidth) < GeometryTolerance;
            previousBounds = bounds;
            previousCoordinateWidth = coordinateWidth;
            hasPreviousBounds = true;
            StopAnimation();
            if (!shouldAnimate)
                return;

            transform.TranslateX = visualBounds.X - bounds.X;
            transform.TranslateY = visualBounds.Y - bounds.Y;
            if (animateSize)
            {
                transform.ScaleX = visualBounds.Width / bounds.Width;
                transform.ScaleY = visualBounds.Height / bounds.Height;
            }

            Storyboard transition = new Storyboard();
            AddAnimation(transition, transform, "TranslateX", transform.TranslateX, 0, remainingMilliseconds);
            AddAnimation(transition, transform, "TranslateY", transform.TranslateY, 0, remainingMilliseconds);
            if (animateSize)
            {
                AddAnimation(transition, transform, "ScaleX", transform.ScaleX, 1, remainingMilliseconds);
                AddAnimation(transition, transform, "ScaleY", transform.ScaleY, 1, remainingMilliseconds);
            }
            transition.Completed += (completedSender, completedArgs) =>
            {
                if (animation == transition)
                    StopAnimation();
            };
            animation = transition;
            transition.Begin();
        }

        /// <summary>Reads a monotonic clock for remaining motion budget, independent of wall-clock changes.</summary>
        /// <returns>Monotonic elapsed milliseconds.</returns>
        private static double MillisecondsNow()
        {
            return Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency;
        }

        /// <summary>Compares arranged bounds with tolerance for compositor coordinate roundoff.</summary>
        /// <param name="left">The previous final rectangle.</param>
        /// <param name="right">The current final rectangle.</param>
        /// <returns>Whether both rectangles describe the same layout.</returns>
        private static bool SameBounds(Rect left, Rect right)
        {
            return Math.Abs(left.X - right.X) < GeometryTolerance
                && Math.Abs(left.Y - right.Y) < GeometryTolerance
                && Math.Abs(left.Width - right.Width) < GeometryTolerance
                && Math.Abs(left.Height - right.Height) < GeometryTolerance;
        }

        /// <summary>Adds an independent render-property animation to one shared transition.</summary>
        /// <param name="transition">The transition receiving the animation.</param>
        /// <param name="target">The render transform or chrome background.</param>
        /// <param name="property">The render-only property to animate.</param>
        /// <param name="from">The current visual value, including interrupted motion.</param>
        /// <param name="to">The final uncorrected layout value.</param>
        /// <param name="milliseconds">Remaining time until the shared transition deadline.</param>
        private static void AddAnimation(Storyboard transition, DependencyObject target,
            string property, double from, double to, double milliseconds)
        {
            DoubleAnimation motion = new DoubleAnimation
            {
                From = from,
                To = to,
                Duration = new Duration(TimeSpan.FromMilliseconds(milliseconds)),
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
            };
            Storyboard.SetTarget(motion, target);
            Storyboard.SetTargetProperty(motion, property);
            transition.Children.Add(motion);
        }

        /// <summary>Stops obsolete timelines and returns the visual to its state-owned layout.</summary>
        private void StopAnimation()
        {
            Storyboard previous = animation;
            animation = null;
            if (previous != null)
                previous.Stop();
            transform.TranslateX = transform.TranslateY = 0;
            transform.ScaleX = transform.ScaleY = 1;
        }
    }
}
