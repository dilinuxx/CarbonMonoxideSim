//
//  BLEViewControllerWrapper.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/07/2025.
//

import SwiftUI
import UIKit

struct BLEViewControllerWrapper: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> BLEViewController {
        return BLEViewController()
    }

    func updateUIViewController(_ uiViewController: BLEViewController, context: Context) {
    }
}

