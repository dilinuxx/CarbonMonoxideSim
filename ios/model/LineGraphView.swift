//
//  LineGraphView.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import SwiftUI

struct LineGraphView: View {
    let values: [(Double, Double, Double)]
    let colors: [Color] = [.red, .blue, .green]

    var body: some View {
        GeometryReader { geo in
            Canvas { context, size in
                let maxVal = values.flatMap { [$0.0, $0.1, $0.2] }.max() ?? 1.0

                for (index, lineIndex) in [0, 1, 2].enumerated() {
                    var path = Path()
                    for i in values.indices {
                        let x = size.width * CGFloat(i) / CGFloat(values.count - 1)
                        let y = size.height * (1 - CGFloat([values[i].0, values[i].1, values[i].2][lineIndex] / maxVal))
                        if i == 0 {
                            path.move(to: CGPoint(x: x, y: y))
                        } else {
                            path.addLine(to: CGPoint(x: x, y: y))
                        }
                    }
                    context.stroke(path, with: .color(colors[index]), lineWidth: 2)
                }
            }
        }
        .frame(height: 150)
        .background(Color(.systemGray6))
        .cornerRadius(10)
        .padding()
    }
}
