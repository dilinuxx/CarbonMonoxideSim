//
//  COAppDelegate.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/08/2025.
//

import UIKit
import Firebase
import UserNotifications

class COAppDelegate: NSObject, UIApplicationDelegate, UNUserNotificationCenterDelegate, MessagingDelegate {
    
    var classifier: Classifier?
    
    var regression: Regression?
    
    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        
        FirebaseApp.configure()

        UNUserNotificationCenter.current().delegate = self
        Messaging.messaging().delegate = self

        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .badge, .sound]) { granted, error in
            print("Notification permission granted: \(granted)")
        }

        application.registerForRemoteNotifications()

        // Try to get the token manually in case delegate method hasn't fired yet
        Messaging.messaging().token { token, error in
            if let error = error {
                print("Error fetching FCM token: \(error.localizedDescription)")
            } else if let token = token {
                print("FCM token fetched manually: \(token)")
                SharedPreference.shared.setValue(token, forKey: Consts.DEVICE_TOKEN)
            }
        }
        
        // Load Machine Learning (ML) Model
        classifier = Classifier()
        regression = Regression()
        
        return true
    }
    
    func application(_ application: UIApplication, didRegisterForRemoteNotificationsWithDeviceToken deviceToken: Data) {
        // Pass the device token to Firebase Messaging
        Messaging.messaging().apnsToken = deviceToken
        
        // Log the token for debugging
        print("APNs device token registered: \(deviceToken.map { String(format: "%02.2hhx", $0) }.joined())")
        
        // Now that APNs token is set, fetch the FCM token again
        Messaging.messaging().token { token, error in
            if let error = error {
                print("Error fetching FCM token: \(error)")
            } else if let token = token {
                print("FCM token fetched after APNs token set: \(token)")
                SharedPreference.shared.setValue(token, forKey: Consts.DEVICE_TOKEN)
            }
        }
    }
    
    func messaging(_ messaging: Messaging, didReceiveRegistrationToken fcmToken: String?) {
        guard let token = fcmToken else {
            print("Firebase Token is nil")
            return
        }
        print("Firebase Token: \(token)")
        SharedPreference.shared.setValue(token, forKey: Consts.DEVICE_TOKEN)
    }
    
    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                willPresent notification: UNNotification,
                                withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void) {
        completionHandler([.banner, .sound, .badge])
    }

    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                didReceive response: UNNotificationResponse,
                                withCompletionHandler completionHandler: @escaping () -> Void) {
        let userInfo = response.notification.request.content.userInfo
        handleNotification(userInfo: userInfo)
        completionHandler()
    }

    func handleNotification(userInfo: [AnyHashable: Any]) {
        guard let type = userInfo["type"] as? String else { return }

        switch type {
        case "chat_notification":
            if let chatData = userInfo["my_chat"] as? String {
                ChatManager.shared.handleChatMessage(chatData)
            }
        case "ticket_comment_notification",
             "wallet_notification",
             "admin_notification":
            NotificationHandler.shared.sendLocalNotification(title: "Forex", body: userInfo["body"] as? String ?? "")
        default:
            NotificationHandler.shared.sendLocalNotification(title: "Forex", body: userInfo["body"] as? String ?? "")
        }
    }
    
    func application(_ application: UIApplication, supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        return .portrait
    }
}



