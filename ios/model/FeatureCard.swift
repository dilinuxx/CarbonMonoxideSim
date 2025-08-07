//
//  FeatureCard.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import SwiftUI

struct FeatureCard: View {
    let icon: String
    let title: String
    let value: String
    let color: Color

    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: icon)
                .font(.largeTitle)
                .foregroundColor(color)

            Text(title)
                .font(.caption)
                .foregroundColor(.gray)

            Text(value)
                .font(.headline)
        }
        .frame(maxWidth: .infinity)
        .padding()
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}
