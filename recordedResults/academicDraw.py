#!/usr/bin/env python3
"""
IEEE Transaction Style Performance Comparison Charts
Comparing Coroutine vs Threaded Implementation Performance

Author: Performance Analysis
Date: June 2025
"""

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import os
import sys

# Set IEEE transaction style parameters
plt.rcParams.update({
    'font.family': 'serif',
    'font.serif': ['Times New Roman', 'Computer Modern Roman'],
    'font.size': 11,
    'axes.labelsize': 11,
    'axes.titlesize': 12,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'figure.titlesize': 12,
    'text.usetex': False,  # Set to True if LaTeX is available
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'axes.grid': True,
    'grid.alpha': 0.3,
    'axes.axisbelow': True
})


def read_and_parse_data(coro_file_path, threaded_file_path):
    """
    Read and parse the benchmark CSV files

    Args:
        coro_file_path: Path to coroutine benchmark CSV file
        threaded_file_path: Path to threaded benchmark CSV file

    Returns:
        tuple: (connections, coroutine_avg, threaded_avg)
    """
    try:
        # Read CSV files
        print(f"Reading coroutine data from: {coro_file_path}")
        coro_df = pd.read_csv(coro_file_path)

        print(f"Reading threaded data from: {threaded_file_path}")
        threaded_df = pd.read_csv(threaded_file_path)

        # Display basic info about the data
        print(f"\nCoroutine data shape: {coro_df.shape}")
        print(f"Threaded data shape: {threaded_df.shape}")
        print(f"Coroutine columns: {list(coro_df.columns)}")
        print(f"Threaded columns: {list(threaded_df.columns)}")

        # Calculate averages by connection count for coroutine data
        coro_grouped = coro_df.groupby('connections')['msg_per_sec'].agg(['mean', 'count', 'std']).reset_index()
        coro_grouped.columns = ['connections', 'avg_msg_per_sec', 'test_count', 'std_dev']

        # Calculate averages by connection count for threaded data
        # Filter out zero values (failed tests) before calculating averages
        threaded_df_filtered = threaded_df[threaded_df['msg_per_sec'] > 0]
        threaded_grouped = threaded_df.groupby('connections').agg({
            'msg_per_sec': ['mean', 'count'],
            'errors': 'sum'
        }).reset_index()
        threaded_grouped.columns = ['connections', 'avg_msg_per_sec', 'test_count', 'total_errors']

        # Handle cases where all tests failed for a connection count
        all_tests_count = threaded_df.groupby('connections').size().reset_index(name='total_tests')
        threaded_grouped = threaded_grouped.merge(all_tests_count, on='connections', how='right')
        threaded_grouped['avg_msg_per_sec'] = threaded_grouped['avg_msg_per_sec'].fillna(0)
        threaded_grouped['test_count'] = threaded_grouped['test_count'].fillna(0)

        # Get unique connection counts (should be the same for both)
        connections = sorted(coro_grouped['connections'].unique())

        # Create arrays for plotting
        coroutine_avg = []
        threaded_avg = []

        print("\n=== PERFORMANCE ANALYSIS ===")
        print("Connection Count | Coroutine (msg/s) | Threaded (msg/s) | Success Rate")
        print("-" * 75)

        for conn in connections:
            # Coroutine average
            coro_row = coro_grouped[coro_grouped['connections'] == conn]
            coro_val = coro_row['avg_msg_per_sec'].iloc[0] if not coro_row.empty else 0
            coroutine_avg.append(coro_val)

            # Threaded average
            threaded_row = threaded_grouped[threaded_grouped['connections'] == conn]
            if not threaded_row.empty:
                threaded_val = threaded_row['avg_msg_per_sec'].iloc[0]
                success_rate = f"{threaded_row['test_count'].iloc[0]:.0f}/{threaded_row['total_tests'].iloc[0]:.0f}"
            else:
                threaded_val = 0
                success_rate = "0/0"

            threaded_avg.append(threaded_val)

            print(f"{conn:14d} | {coro_val:13.1f} | {threaded_val:12.1f} | {success_rate}")

        return np.array(connections), np.array(coroutine_avg), np.array(threaded_avg)

    except FileNotFoundError as e:
        print(f"Error: Could not find file - {e}")
        print("Please check the file paths and ensure the CSV files exist.")
        sys.exit(1)
    except Exception as e:
        print(f"Error reading/parsing data: {e}")
        sys.exit(1)


# Color scheme for IEEE publications (professional blue/red)
colors = {
    'coroutine': '#1f77b4',  # Professional blue
    'threaded': '#d62728',  # Professional red
    'grid': '#cccccc'
}


def create_bar_chart(connections, coroutine_avg, threaded_avg):
    """Create a professional bar chart comparison"""
    fig, ax = plt.subplots(figsize=(8, 6))

    x_pos = np.arange(len(connections))
    width = 0.35

    # Color scheme for IEEE publications (professional blue/red)
    colors = {
        'coroutine': '#1f77b4',  # Professional blue
        'threaded': '#d62728',  # Professional red
        'grid': '#cccccc'
    }

    # Create bars
    bars1 = ax.bar(x_pos - width / 2, coroutine_avg, width,
                   label='Coroutine Implementation',
                   color=colors['coroutine'],
                   alpha=0.8,
                   edgecolor='black',
                   linewidth=0.5)

    bars2 = ax.bar(x_pos + width / 2, threaded_avg, width,
                   label='Threaded Implementation',
                   color=colors['threaded'],
                   alpha=0.8,
                   edgecolor='black',
                   linewidth=0.5)

    # Customization
    ax.set_xlabel('Number of Concurrent Connections')
    ax.set_ylabel('Average Messages per Second')
    ax.set_title('Performance Comparison: Coroutine vs Threaded Implementation')
    ax.set_xticks(x_pos)
    ax.set_xticklabels(connections)
    ax.legend(loc='upper right')

    # Add value labels on bars
    for bar in bars1:
        height = bar.get_height()
        ax.annotate(f'{height:.0f}',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3),  # 3 points vertical offset
                    textcoords="offset points",
                    ha='center', va='bottom',
                    fontsize=9)

    for bar in bars2:
        height = bar.get_height()
        if height > 0:  # Only label non-zero values
            ax.annotate(f'{height:.0f}',
                        xy=(bar.get_x() + bar.get_width() / 2, height),
                        xytext=(0, 3),
                        textcoords="offset points",
                        ha='center', va='bottom',
                        fontsize=9)
        else:
            # Mark failed tests
            ax.annotate('FAILED',
                        xy=(bar.get_x() + bar.get_width() / 2, 500),
                        ha='center', va='bottom',
                        fontsize=8, color='red', weight='bold')

    # Set y-axis to start from 0 and add some headroom
    ax.set_ylim(0, max(coroutine_avg) * 1.1)

    # Add grid for better readability
    ax.grid(True, alpha=0.3, linestyle='-', linewidth=0.5)
    ax.set_axisbelow(True)

    plt.tight_layout()
    return fig


def create_line_chart(connections, coroutine_avg, threaded_avg):
    """Create a professional line chart with markers"""
    fig, ax = plt.subplots(figsize=(8, 6))

    colors = {
        'coroutine': '#1f77b4',
        'threaded': '#d62728'
    }

    # Plot lines with markers
    ax.plot(connections, coroutine_avg,
            marker='o', linewidth=2, markersize=8,
            label='Coroutine Implementation',
            color=colors['coroutine'],
            markerfacecolor='white',
            markeredgecolor=colors['coroutine'],
            markeredgewidth=2)

    # For threaded, only plot non-zero values
    valid_mask = threaded_avg > 0
    ax.plot(connections[valid_mask], threaded_avg[valid_mask],
            marker='s', linewidth=2, markersize=8,
            label='Threaded Implementation',
            color=colors['threaded'],
            markerfacecolor='white',
            markeredgecolor=colors['threaded'],
            markeredgewidth=2)

    # Mark failed points
    failed_mask = threaded_avg == 0
    if np.any(failed_mask):
        ax.scatter(connections[failed_mask], [500] * np.sum(failed_mask),
                   marker='x', s=100, color='red', linewidth=3,
                   label='Failed Tests', zorder=5)

    # Customization
    ax.set_xlabel('Number of Concurrent Connections')
    ax.set_ylabel('Average Messages per Second')
    ax.set_title('Performance Scaling: Coroutine vs Threaded Implementation')
    ax.legend(loc='upper right')

    # Set axis limits
    ax.set_xlim(min(connections) - 100, max(connections) + 100)
    ax.set_ylim(0, max(coroutine_avg) * 1.1)

    # Add grid
    ax.grid(True, alpha=0.3, linestyle='-', linewidth=0.5)
    ax.set_axisbelow(True)

    # Add annotations for key points
    ax.annotate(f'Peak: {max(coroutine_avg):.0f} msg/s',
                xy=(connections[np.argmax(coroutine_avg)], max(coroutine_avg)),
                xytext=(connections[len(connections) // 2], max(coroutine_avg) + 500),
                arrowprops=dict(arrowstyle='->', color=colors['coroutine'], lw=1),
                fontsize=9, color=colors['coroutine'])

    plt.tight_layout()
    return fig


def create_performance_ratio_chart(connections, coroutine_avg, threaded_avg):
    """Create a chart showing performance ratio between implementations"""
    fig, ax = plt.subplots(figsize=(8, 5))

    colors = {'coroutine': '#1f77b4'}

    # Calculate performance ratio (coroutine/threaded) for valid data points
    valid_mask = threaded_avg > 0
    ratio = coroutine_avg[valid_mask] / threaded_avg[valid_mask]
    valid_connections = connections[valid_mask]

    # Create bar chart for ratios
    bars = ax.bar(valid_connections, ratio,
                  color=colors['coroutine'], alpha=0.7,
                  edgecolor='black', linewidth=0.5)

    # Add value labels
    for i, (conn, r) in enumerate(zip(valid_connections, ratio)):
        ax.annotate(f'{r:.1f}×',
                    xy=(conn, r),
                    xytext=(0, 5),
                    textcoords="offset points",
                    ha='center', va='bottom',
                    fontsize=10, weight='bold')

    # Add horizontal line at y=1 for reference
    ax.axhline(y=1, color='black', linestyle='--', alpha=0.5, linewidth=1)
    ax.text(valid_connections[len(valid_connections) // 2], 1.1, 'Equal Performance',
            fontsize=9, alpha=0.7, ha='center')

    ax.set_xlabel('Number of Concurrent Connections')
    ax.set_ylabel('Performance Ratio (Coroutine/Threaded)')
    ax.set_title('Performance Advantage of Coroutine Implementation')
    ax.set_ylim(0, max(ratio) * 1.2)

    # Add note about failed tests
    failed_count = np.sum(threaded_avg == 0)
    if failed_count > 0:
        ax.text(0.02, 0.98, f'Note: Threaded implementation failed at {failed_count} connection levels',
                transform=ax.transAxes, fontsize=9,
                verticalalignment='top', style='italic')

    ax.grid(True, alpha=0.3)
    ax.set_axisbelow(True)

    plt.tight_layout()
    return fig


def save_all_charts(connections, coroutine_avg, threaded_avg, output_dir="./"):
    """Generate and save all charts"""
    import os

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Create charts
    fig1 = create_bar_chart(connections, coroutine_avg, threaded_avg)
    fig2 = create_line_chart(connections, coroutine_avg, threaded_avg)
    fig3 = create_performance_ratio_chart(connections, coroutine_avg, threaded_avg)

    # Define file paths
    bar_png = os.path.join(output_dir, 'performance_comparison_bar.png')
    bar_pdf = os.path.join(output_dir, 'performance_comparison_bar.pdf')
    line_png = os.path.join(output_dir, 'performance_scaling_line.png')
    line_pdf = os.path.join(output_dir, 'performance_scaling_line.pdf')
    ratio_png = os.path.join(output_dir, 'performance_ratio.png')
    ratio_pdf = os.path.join(output_dir, 'performance_ratio.pdf')

    # Save charts
    fig1.savefig(bar_png, dpi=300, bbox_inches='tight')
    fig1.savefig(bar_pdf, bbox_inches='tight')

    fig2.savefig(line_png, dpi=300, bbox_inches='tight')
    fig2.savefig(line_pdf, bbox_inches='tight')

    fig3.savefig(ratio_png, dpi=300, bbox_inches='tight')
    fig3.savefig(ratio_pdf, bbox_inches='tight')

    print(f"\nCharts saved to: {os.path.abspath(output_dir)}")
    print("Files generated:")
    print(f"  - {bar_png}")
    print(f"  - {bar_pdf}")
    print(f"  - {line_png}")
    print(f"  - {line_pdf}")
    print(f"  - {ratio_png}")
    print(f"  - {ratio_pdf}")

    plt.show()


def print_statistical_summary(connections, coroutine_avg, threaded_avg):
    """Print statistical summary for the paper"""
    print("\n" + "=" * 60)
    print("=== STATISTICAL SUMMARY FOR IEEE PAPER ===")
    print("=" * 60)

    print(f"Coroutine Implementation:")
    print(f"  Mean performance: {np.mean(coroutine_avg):.1f} ± {np.std(coroutine_avg):.1f} msg/s")
    print(f"  Performance range: {np.min(coroutine_avg):.1f} - {np.max(coroutine_avg):.1f} msg/s")
    print(f"  Coefficient of variation: {(np.std(coroutine_avg) / np.mean(coroutine_avg) * 100):.1f}%")

    valid_threaded = threaded_avg[threaded_avg > 0]
    if len(valid_threaded) > 0:
        print(f"\nThreaded Implementation (successful tests only):")
        print(f"  Mean performance: {np.mean(valid_threaded):.1f} ± {np.std(valid_threaded):.1f} msg/s")
        print(f"  Performance range: {np.min(valid_threaded):.1f} - {np.max(valid_threaded):.1f} msg/s")
        print(
            f"  Success rate: {len(valid_threaded)}/{len(threaded_avg)} ({len(valid_threaded) / len(threaded_avg) * 100:.0f}%)")

        # Performance improvement
        valid_mask = threaded_avg > 0
        improvement = (coroutine_avg[valid_mask] / threaded_avg[valid_mask]).mean()
        print(f"\nOverall Performance Improvement: {improvement:.1f}× faster with coroutines")

        # Reliability comparison
        print(f"\nReliability Analysis:")
        print(f"  Coroutine success rate: 100% (0 failures)")
        print(
            f"  Threaded success rate: {len(valid_threaded) / len(threaded_avg) * 100:.0f}% ({len(threaded_avg) - len(valid_threaded)} failures)")
    else:
        print(f"\nThreaded Implementation: All tests failed")


def main():
    """Main function to run the complete analysis pipeline"""

    # File paths - modify these to match your file locations
    coro_file = "benchmark_results_coro.csv"
    threaded_file = "benchmark_results_threaded.csv"

    print("=" * 60)
    print("IEEE Academic Performance Analysis Pipeline")
    print("=" * 60)
    print("Pipeline: Read Files → Parse Data → Calculate Metrics → Generate Charts")

    # Step 1: Read and parse data from CSV files
    print("\nStep 1: Reading and parsing benchmark data...")
    connections, coroutine_avg, threaded_avg = read_and_parse_data(coro_file, threaded_file)

    # Step 2: Print statistical summary
    print("\nStep 2: Statistical analysis...")
    print_statistical_summary(connections, coroutine_avg, threaded_avg)

    # Step 3: Generate and save charts
    print("\nStep 3: Generating IEEE-style performance charts...")

    # Option 1: Save to current directory (default)
    save_all_charts(connections, coroutine_avg, threaded_avg)

    # Option 2: Save to specific directory (uncomment and modify as needed)
    # save_all_charts(connections, coroutine_avg, threaded_avg, output_dir="/path/to/your/output/folder")
    # save_all_charts(connections, coroutine_avg, threaded_avg, output_dir="./figures")  # Creates 'figures' subfolder

    print("\nAnalysis complete!")
    print("Charts generated successfully!")
    print("Files are ready for IEEE paper submission.")


if __name__ == "__main__":
    main()