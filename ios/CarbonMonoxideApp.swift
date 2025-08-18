//
//  CarbonMonoxide.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/08/2025.
//

import SwiftUI

@main
struct CarbonMonoxide: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    var body: some Scene {
        WindowGroup {
            BLEViewControllerWrapper()
        }
    }
}
