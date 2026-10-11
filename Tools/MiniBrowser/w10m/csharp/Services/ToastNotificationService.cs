using System;
using Windows.Data.Xml.Dom;
using Windows.UI.Notifications;
using WebKitMiniBrowser.Services.Interfaces;

namespace WebKitMiniBrowser.Services
{
    /// <summary>Displays notifications using the Windows toast notification system.</summary>
    public sealed class ToastNotificationService : IToastNotificationService
    {
        /// <summary>Creates and shows a two-line Windows toast.</summary>
        /// <param name="title">The notification title.</param>
        /// <param name="message">The notification body.</param>
        public void Show(string title, string message)
        {
            if (string.IsNullOrEmpty(title) && string.IsNullOrEmpty(message))
                return;

            XmlDocument toastXml = ToastNotificationManager.GetTemplateContent(ToastTemplateType.ToastText02);
            XmlNodeList textNodes = toastXml.GetElementsByTagName("text");
            textNodes.Item(0).AppendChild(toastXml.CreateTextNode(title ?? string.Empty));
            textNodes.Item(1).AppendChild(toastXml.CreateTextNode(message ?? string.Empty));
            ToastNotificationManager.CreateToastNotifier().Show(new ToastNotification(toastXml));
        }
    }
}
