#!/usr/bin/env python3
"""
3D Point Cloud Visualization with PCA
Reads a CSV file with x,y,z coordinates and visualizes the points.
Also performs PCA and shows the principal components.
"""

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots
from sklearn.decomposition import PCA
import sys


def load_points(filename):
    """Load points from CSV file."""
    try:
        df = pd.read_csv(filename)
        if not all(col in df.columns for col in ['x', 'y', 'z']):
            raise ValueError("CSV must have columns: x, y, z")
        return df[['x', 'y', 'z']].values
    except Exception as e:
        print(f"Error loading file: {e}")
        sys.exit(1)


def perform_pca(points):
    """Perform PCA and return transformed points and PCA object."""
    pca = PCA(n_components=3)
    points_pca = pca.fit_transform(points)
    return points_pca, pca


def create_fancy_visualization(points, points_pca, pca, filename):
    """Create interactive visualization with original and PCA-transformed points."""
    
    # Create subplot with 1 row, 2 columns
    fig = make_subplots(
        rows=1, cols=2,
        subplot_titles=('Original Point Cloud', 'PCA Transformed (Main Components)'),
        specs=[[{'type': 'scatter3d'}, {'type': 'scatter3d'}]]
    )
    
    # Original points
    fig.add_trace(
        go.Scatter3d(
            x=points[:, 0],
            y=points[:, 1],
            z=points[:, 2],
            mode='markers',
            marker=dict(
                size=2,
                color=points[:, 2],  # Color by z-coordinate
                colorscale='Viridis',
                showscale=True,
                colorbar=dict(title="Z", x=0.45)
            ),
            name='Points'
        ),
        row=1, col=1
    )
    
    # PCA transformed points
    fig.add_trace(
        go.Scatter3d(
            x=points_pca[:, 0],
            y=points_pca[:, 1],
            z=points_pca[:, 2],
            mode='markers',
            marker=dict(
                size=2,
                color=points_pca[:, 0],  # Color by first principal component
                colorscale='Plasma',
                showscale=True,
                colorbar=dict(title="PC1", x=1.0)
            ),
            name='PCA Points'
        ),
        row=1, col=2
    )
    
    # Add principal component vectors (as arrows from origin)
    center = np.mean(points, axis=0)
    scale = np.max(np.std(points, axis=0)) * 3  # Scale for visibility
    
    colors = ['red', 'green', 'blue']
    for i in range(3):
        component = pca.components_[i] * scale
        
        # Arrow from center to component direction
        fig.add_trace(
            go.Scatter3d(
                x=[center[0], center[0] + component[0]],
                y=[center[1], center[1] + component[1]],
                z=[center[2], center[2] + component[2]],
                mode='lines+text',
                line=dict(color=colors[i], width=6),
                text=['', f'PC{i+1}'],
                textposition='top center',
                name=f'PC{i+1} ({pca.explained_variance_ratio_[i]:.1%})',
                showlegend=True
            ),
            row=1, col=1
        )
    
    # Update layout
    fig.update_layout(
        title=dict(
            text=f'Point Cloud Visualization - {len(points)} points<br>' +
                 f'<sub>Variance explained: PC1={pca.explained_variance_ratio_[0]:.1%}, ' +
                 f'PC2={pca.explained_variance_ratio_[1]:.1%}, ' +
                 f'PC3={pca.explained_variance_ratio_[2]:.1%}</sub>',
            x=0.5,
            xanchor='center'
        ),
        height=700,
        showlegend=True
    )
    
    # Update axes labels
    fig.update_scenes(
        xaxis_title='X',
        yaxis_title='Y',
        zaxis_title='Z',
        row=1, col=1
    )
    
    fig.update_scenes(
        xaxis_title='PC1',
        yaxis_title='PC2',
        zaxis_title='PC3',
        row=1, col=2
    )
    
    return fig

def create_visualization(points, points_pca, pca, filename):
    """Create interactive visualization with original points and PCA vectors."""
    
    # Create single 3D plot
    fig = go.Figure()
    
    # Original points - single color
    fig.add_trace(
        go.Scatter3d(
            x=points[:, 0],
            y=points[:, 1],
            z=points[:, 2],
            mode='markers',
            marker=dict(
                size=2,
                color='lightsalmon', # https://masamasace.github.io/plotly_color/
            ),
            name='Points'
        )
    )
    
    # Add principal component vectors (as arrows from center)
    center = np.mean(points, axis=0)
    scale = np.max(np.std(points, axis=0)) * 3  # Scale for visibility
    
    colors = ['red', 'green', 'blue']
    for i in range(3):
        component = pca.components_[i] * scale
        
        # Arrow from center to component direction
        fig.add_trace(
            go.Scatter3d(
                x=[center[0], center[0] + component[0]],
                y=[center[1], center[1] + component[1]],
                z=[center[2], center[2] + component[2]],
                mode='lines+text',
                line=dict(color=colors[i], width=6),
                text=['', f'PC{i+1}'],
                textposition='top center',
                name=f'PC{i+1} ({pca.explained_variance_ratio_[i]:.1%})',
                showlegend=True
            )
        )
    
    # Update layout
    fig.update_layout(
        title=dict(
            text=f'Point Cloud Visualization - {len(points)} points<br>' +
                 f'<sub>Variance explained: PC1={pca.explained_variance_ratio_[0]:.1%}, ' +
                 f'PC2={pca.explained_variance_ratio_[1]:.1%}, ' +
                 f'PC3={pca.explained_variance_ratio_[2]:.1%}</sub>',
            x=0.5,
            xanchor='center'
        ),
        scene=dict(
            xaxis_title='X',
            yaxis_title='Y',
            zaxis_title='Z'
        ),
        height=800,
        showlegend=True
    )
    
    return fig


def print_pca_summary(pca, points):
    """Print summary of PCA results."""
    print("\n" + "="*60)
    print("PCA ANALYSIS SUMMARY")
    print("="*60)
    print(f"\nTotal points: {len(points)}")
    print(f"\nExplained variance ratio:")
    for i, var in enumerate(pca.explained_variance_ratio_):
        print(f"  PC{i+1}: {var:.4f} ({var*100:.2f}%)")
    print(f"\nCumulative variance: {sum(pca.explained_variance_ratio_):.4f}")
    
    print(f"\nPrincipal components (eigenvectors):")
    for i, component in enumerate(pca.components_):
        print(f"  PC{i+1}: [{component[0]:8.4f}, {component[1]:8.4f}, {component[2]:8.4f}]")
    
    print(f"\nSingular values:")
    for i, val in enumerate(pca.singular_values_):
        print(f"  PC{i+1}: {val:.4f}")
    print("="*60 + "\n")


def main():
    if len(sys.argv) < 2:
        filename = "./data_visualization/pca_data.csv"
        print(f"No filename provided, using default: {filename}")
    else:
        filename = sys.argv[1]
    
    print(f"Loading points from {filename}...")
    points = load_points(filename)
    print(f"Loaded {len(points)} points")
    
    print("Performing PCA...")
    points_pca, pca = perform_pca(points)
    
    print_pca_summary(pca, points)
    
    print("Creating visualization...")
    fig = create_visualization(points, points_pca, pca, filename)
    
    print("Opening interactive plot in browser...")
    # fig.show()
    output_file = filename.replace('.csv', '_visualization.html')
    fig.write_html(output_file)
    print(f"Visualization saved to: {output_file}")
    print("Open this file in your web browser to view the interactive plot.")


if __name__ == "__main__":
    main()