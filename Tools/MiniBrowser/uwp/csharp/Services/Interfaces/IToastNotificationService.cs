namespace WebKitMiniBrowser.Services.Interfaces
{
    /// <summary>Displays user notifications using the Windows notification system.</summary>
    public interface IToastNotificationService
    {
        /// <summary>Shows a system toast with a title and body.</summary>
        /// <param name="title">The toast title.</param>
        /// <param name="message">The toast body text.</param>
        void Show(string title, string message);
    }
}
